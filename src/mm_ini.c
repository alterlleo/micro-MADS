#include "mm_ini.h"

#include <string.h>

enum { T_OTHER = 0, T_AGENTS, T_SECTION };

static int is_space(char c) { return c == ' ' || c == '\t'; }

static char *trim(char *s) {
  char *e;
  while (is_space(*s)) s++;
  e = s + strlen(s);
  while (e > s && is_space(e[-1])) *--e = '\0';
  return s;
}

/* Cuts a '#' comment that is not inside a quoted string. */
static void strip_comment(char *s) {
  char quote = 0;
  for (; *s; s++) {
    if (quote) {
      if (*s == '\\' && quote == '"' && s[1]) s++;
      else if (*s == quote) quote = 0;
    } else if (*s == '"' || *s == '\'') {
      quote = *s;
    } else if (*s == '#') {
      *s = '\0';
      return;
    }
  }
}

/* Reads a "basic" or 'literal' TOML string starting at *p into out.
 * Advances *p past it. Returns 0, or -1 if it is not a string or too long. */
static int read_string(const char **p, char *out, size_t cap) {
  const char *s = *p;
  char quote = *s++;
  size_t n = 0;
  if (quote != '"' && quote != '\'') return -1;
  while (*s && *s != quote) {
    char c = *s++;
    if (c == '\\' && quote == '"' && *s) {
      c = *s++;
      if (c == 'n') c = '\n';
      else if (c == 't') c = '\t';
    }
    if (n + 1 >= cap) return -1;
    out[n++] = c;
  }
  if (*s != quote) return -1;
  out[n] = '\0';
  *p = s + 1;
  return 0;
}

/* Number after the last ':' of "tcp://host:port". */
static uint16_t port_of(const char *value) {
  char uri[64];
  const char *colon;
  unsigned long port = 0;
  if (read_string(&value, uri, sizeof(uri)) != 0) return 0;
  colon = strrchr(uri, ':');
  if (!colon || !colon[1]) return 0;
  for (colon++; *colon; colon++) {
    if (*colon < '0' || *colon > '9') return 0;
    port = port * 10 + (unsigned long)(*colon - '0');
    if (port > 65535) return 0;
  }
  return (uint16_t)port;
}

static uint16_t uint_of(const char *s) {
  unsigned long v = 0;
  if (*s < '0' || *s > '9') return 0;
  for (; *s >= '0' && *s <= '9'; s++) {
    v = v * 10 + (unsigned long)(*s - '0');
    if (v > 65535) return 0;
  }
  return (uint16_t)v;
}

static void add_sub_topic(mm_ini_t *ini, const char **p) {
  char topic[MM_MAX_TOPIC_LEN];
  if (read_string(p, topic, sizeof(topic)) != 0) {
    /* too long (or unterminated): skip it up to its closing quote */
    char quote = **p;
    const char *close = NULL;
    if (quote == '"' || quote == '\'') close = strchr(*p + 1, quote);
    *p = close ? close + 1 : *p + strlen(*p);
    return;
  }
  if (ini->sub_topic_count < MM_MAX_SUB_TOPICS) {
    strcpy(ini->sub_topics[ini->sub_topic_count++], topic);
  }
}

/* Array elements, possibly spread over several lines, until ']'. */
static void parse_array(mm_ini_t *ini, const char *p) {
  while (*p) {
    if (*p == ']') {
      ini->in_array = 0;
      return;
    }
    if (*p == '"' || *p == '\'') add_sub_topic(ini, &p);
    else p++;
  }
  ini->in_array = 1; /* continues on the next line */
}

static void parse_header(mm_ini_t *ini, char *s) {
  char *end = strchr(s, ']');
  char *name;
  ini->table = T_OTHER;
  ini->in_array = 0;
  if (s[1] == '[' || !end) return; /* [[array of tables]] or malformed */
  *end = '\0';
  name = trim(s + 1);
  if (*name == '"' || *name == '\'') { /* ["name"] */
    size_t n = strlen(name);
    if (n < 2 || name[n - 1] != name[0]) return;
    name[n - 1] = '\0';
    name++;
  }
  if (strcmp(name, "agents") == 0) {
    ini->table = T_AGENTS;
  } else if (ini->section && strcmp(name, ini->section) == 0) {
    ini->table = T_SECTION;
    ini->section_found = 1;
  }
}

static void parse_line(mm_ini_t *ini) {
  char *s, *eq, *key, *value;

  ini->line[ini->len] = '\0';
  s = ini->line;
  if ((uint8_t)s[0] == 0xEF && (uint8_t)s[1] == 0xBB && (uint8_t)s[2] == 0xBF)
    s += 3; /* UTF-8 byte order mark */
  strip_comment(s);
  s = trim(s);

  if (ini->in_array) {
    parse_array(ini, s);
    return;
  }
  if (*s == '\0') return;
  if (*s == '[') {
    parse_header(ini, s);
    return;
  }
  if (ini->table == T_OTHER) return;

  eq = strchr(s, '=');
  if (!eq) return;
  *eq = '\0';
  key = trim(s);
  value = trim(eq + 1);
  if (*key == '"' || *key == '\'') { /* "quoted" key */
    size_t n = strlen(key);
    if (n >= 2 && key[n - 1] == key[0]) {
      key[n - 1] = '\0';
      key++;
    }
  }

  if (ini->table == T_AGENTS) {
    if (strcmp(key, "frontend_address") == 0) ini->frontend_port = port_of(value);
    else if (strcmp(key, "backend_address") == 0) ini->backend_port = port_of(value);
    else if (strcmp(key, "timecode_fps") == 0) ini->timecode_fps = uint_of(value);
  } else if (strcmp(key, "pub_topic") == 0) {
    const char *p = value;
    ini->has_pub_topic = read_string(&p, ini->pub_topic, sizeof(ini->pub_topic)) == 0;
    if (!ini->has_pub_topic) ini->pub_topic[0] = '\0';
  } else if (strcmp(key, "sub_topic") == 0) {
    const char *p = value;
    ini->has_sub_topic = 1;
    ini->sub_topic_count = 0; /* a later definition replaces an earlier one */
    if (*p == '[') parse_array(ini, p + 1);
    else add_sub_topic(ini, &p);
  }
}

void mm_ini_init(mm_ini_t *ini, const char *section) {
  memset(ini, 0, sizeof(*ini));
  ini->section = section;
}

void mm_ini_feed(mm_ini_t *ini, const uint8_t *data, size_t len) {
  size_t i;
  for (i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '\n') {
      if (!ini->skip) parse_line(ini);
      ini->skip = 0;
      ini->len = 0;
    } else if (c == '\r' || ini->skip) {
      /* ignored */
    } else if (ini->len + 1 >= MM_INI_LINE_LEN) {
      ini->skip = 1; /* too long for us: drop the whole line, never split it */
    } else {
      ini->line[ini->len++] = c;
    }
  }
}

void mm_ini_finish(mm_ini_t *ini) {
  if (ini->len > 0 && !ini->skip) parse_line(ini);
  ini->len = 0;
  ini->skip = 0;
}
