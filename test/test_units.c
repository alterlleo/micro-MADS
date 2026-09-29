/* Unit tests without network: ini parser and ZMTP frame reader. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mm_ini.h"
#include "mm_zmtp.h"

static int failures = 0;
#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      failures++;                                                         \
      fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #c);     \
    }                                                                     \
  } while (0)

static char *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  char *buf;
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *len = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  buf = malloc(*len + 1);
  if (fread(buf, 1, *len, f) != *len) *len = 0;
  fclose(f);
  return buf;
}

/* Parses text for `section`, feeding it `chunk` bytes at a time (0 = all). */
static void parse(mm_ini_t *ini, const char *section, const char *text, size_t len,
                  size_t chunk) {
  size_t i;
  mm_ini_init(ini, section);
  if (chunk == 0) chunk = len;
  for (i = 0; i < len; i += chunk)
    mm_ini_feed(ini, (const uint8_t *)text + i, len - i < chunk ? len - i : chunk);
  mm_ini_finish(ini);
}

static void test_real_ini(void) {
  static const size_t chunks[] = {0, 1, 7, 64};
  size_t len, c;
  char *text = read_file("data/mads.ini", &len);
  mm_ini_t ini;
  CHECK(text != NULL);
  if (!text) return;

  for (c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
    /* last section of a 5.6 KB file: what the old 512-byte buffer lost */
    parse(&ini, "stm32_board", text, len, chunks[c]);
    CHECK(ini.frontend_port == 9090 && ini.backend_port == 9091);
    CHECK(ini.timecode_fps == 200);
    CHECK(ini.section_found && ini.has_pub_topic);
    CHECK(strcmp(ini.pub_topic, "micro") == 0);
    CHECK(ini.sub_topic_count == 1 && strcmp(ini.sub_topics[0], "test") == 0);
  }

  parse(&ini, "espressniff", text, len, 0); /* sub_topic as a plain string */
  CHECK(strcmp(ini.pub_topic, "sensors") == 0);
  CHECK(ini.sub_topic_count == 1 && strcmp(ini.sub_topics[0], "command") == 0);

  parse(&ini, "pos_estimator", text, len, 0); /* array of 4 topics */
  CHECK(ini.sub_topic_count == 4 && strcmp(ini.sub_topics[3], "replay_realsense") == 0);

  /* [fmu_multibody_model.parameters] must not be taken as the section */
  parse(&ini, "fmu_multibody_model", text, len, 0);
  CHECK(strcmp(ini.pub_topic, "fmu") == 0);
  CHECK(ini.sub_topic_count == 1 && strcmp(ini.sub_topics[0], "setpoint") == 0);

  parse(&ini, "logger", text, len, 0); /* commented alternative is ignored */
  CHECK(!ini.has_pub_topic && ini.sub_topic_count == 1 && strcmp(ini.sub_topics[0], "edge") == 0);

  parse(&ini, "no_such_agent", text, len, 1);
  CHECK(!ini.section_found && !ini.has_pub_topic && ini.sub_topic_count == 0);
  CHECK(ini.frontend_port == 9090);
  free(text);
}

static void test_ini_cases(void) {
  const char *text =
      "\xEF\xBB\xBF# comment [micro]\r\n"
      "[agents]\r\n"
      "frontend_address = \"tcp://localhost:19090\" # trailing comment\r\n"
      "backend_address='tcp://10.0.0.1:19091'\r\n"
      "[ \"micro\" ]\n"
      "pub_topic = \"with # hash\"\n"
      "long = \"" /* longer than MM_INI_LINE_LEN: must be skipped whole */
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "[fake]\"\n"
      "sub_topic = [\n"
      "  \"a\", # first\n"
      "  'b',\n"
      "  \"a topic name that is far too long for the topic buffer, skipped\",\n"
      "]\n"
      "[micro.sub]\n"
      "pub_topic = \"wrong\"\n"
      "[[micro]]\n"
      "pub_topic = \"wrong\"";
  mm_ini_t ini;
  parse(&ini, "micro", text, strlen(text), 3);
  CHECK(ini.frontend_port == 19090 && ini.backend_port == 19091);
  CHECK(ini.timecode_fps == 0);
  CHECK(ini.section_found);
  CHECK(strcmp(ini.pub_topic, "with # hash") == 0);
  CHECK(ini.sub_topic_count == 2);
  CHECK(strcmp(ini.sub_topics[0], "a") == 0 && strcmp(ini.sub_topics[1], "b") == 0);
}

/* A broker-side stream: greeting, READY, then a 3-frame message whose last
 * frame is 300 bytes (8-byte length form). */
static size_t build_stream(uint8_t *s) {
  size_t n = 0, i;
  uint8_t greeting[64] = {0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0x7F, 3, 1, 'N', 'U', 'L', 'L'};
  memcpy(s, greeting, 64);
  n = 64;
  n += mm_zmtp_ready(s + n, "XPUB");
  n += mm_zmtp_header(s + n, 4, MM_ZMTP_FLAG_MORE);
  memcpy(s + n, "test", 4);
  n += 4;
  n += mm_zmtp_header(s + n, 0, MM_ZMTP_FLAG_MORE); /* empty frame */
  n += mm_zmtp_header(s + n, 300, 0);
  for (i = 0; i < 300; i++) s[n++] = (uint8_t)i;
  return n;
}

static void test_zmtp(void) {
  uint8_t s[512];
  size_t len = build_stream(s), step;

  for (step = 1; step <= len; step = step * 3 + 1) {
    mm_zmtp_decoder_t d;
    int greetings = 0, begins = 0, ends = 0, errors = 0;
    uint64_t data = 0;
    size_t i;
    mm_zmtp_decoder_init(&d);
    for (i = 0; i < len; i += step) {
      const uint8_t *p = s + i;
      size_t n = len - i < step ? len - i : step;
      for (;;) {
        mm_zmtp_ev_t ev;
        size_t used = mm_zmtp_decode(&d, p, n, &ev);
        p += used;
        n -= used;
        if (ev.type == MM_ZMTP_NONE) break;
        if (ev.type == MM_ZMTP_GREETING) greetings++;
        if (ev.type == MM_ZMTP_BEGIN) begins++;
        if (ev.type == MM_ZMTP_DATA) data += ev.len;
        if (ev.type == MM_ZMTP_END) ends++;
        if (ev.type == MM_ZMTP_ERROR) {
          errors++;
          break;
        }
      }
    }
    CHECK(greetings == 1 && errors == 0);
    CHECK(begins == 4 && ends == 4); /* READY + 3 frames */
    CHECK(data == 26 + 4 + 0 + 300); /* READY("XPUB") body is 26 bytes */
  }

  { /* not a ZMTP peer (e.g. an HTTP server) */
    mm_zmtp_decoder_t d;
    mm_zmtp_ev_t ev;
    mm_zmtp_decoder_init(&d);
    mm_zmtp_decode(&d, (const uint8_t *)"HTTP/1.1", 8, &ev);
    CHECK(ev.type == MM_ZMTP_ERROR);
  }
  { /* our READY for REQ matches the bytes libzmq expects */
    uint8_t r[64];
    static const uint8_t expected[] = {0x04, 25, 5, 'R', 'E', 'A', 'D', 'Y', 11,
                                       'S', 'o', 'c', 'k', 'e', 't', '-', 'T', 'y',
                                       'p', 'e', 0, 0, 0, 3, 'R', 'E', 'Q'};
    CHECK(mm_zmtp_ready(r, "REQ") == sizeof(expected));
    CHECK(memcmp(r, expected, sizeof(expected)) == 0);
  }
}

int main(void) {
  test_real_ini();
  test_ini_cases();
  test_zmtp();
  printf("test_units: %s\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
