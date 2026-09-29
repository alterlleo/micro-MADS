#include <stdio.h>
#include <string.h>

#include "mm_fsm.h"
#include "mm_internal.h"

#ifndef MM_HOSTNAME
#if defined(USE_ESP32)
#define MM_HOSTNAME "esp32"
#elif defined(USE_W5500)
#define MM_HOSTNAME "w5500"
#else
#define MM_HOSTNAME "micromads"
#endif
#endif

#define MAX_READ_PER_SPIN 2048 /* bytes read per link per spin, bounds its duration */

/* compile-time check of the topic overrides */
#ifdef PUB_TOPIC
typedef char mm_pub_topic_too_long[sizeof(PUB_TOPIC) <= MM_MAX_TOPIC_LEN ? 1 : -1];
#endif
#ifdef SUB_TOPIC
typedef char mm_sub_topic_too_long[sizeof(SUB_TOPIC) <= MM_MAX_TOPIC_LEN ? 1 : -1];
#endif

/* MADS frame header: "MADS", header version 1, format JSON, no compression,
 * no flags, schema 0. Sent as [topic][header][json], the uncompressed form
 * every MADS >= 2.4 agent reads. */
static const uint8_t mads_header[12] = {'M', 'A', 'D', 'S', 1, 0, 0, 0, 0, 0, 0, 0};

/* ---------------------------------------------------------------------------
 * Building messages
 * ------------------------------------------------------------------------- */

typedef struct {
  uint8_t *buf;
  size_t cap, len;
  bool ok;
} builder_t;

static void put_frame(builder_t *b, const void *data, size_t len, bool more) {
  uint8_t hdr[MM_ZMTP_HEADER_MAX];
  size_t hl = mm_zmtp_header(hdr, len, more ? MM_ZMTP_FLAG_MORE : 0);
  if (!b->ok || b->len + hl + len > b->cap) {
    b->ok = false;
    return;
  }
  memcpy(b->buf + b->len, hdr, hl);
  memcpy(b->buf + b->len + hl, data, len);
  b->len += hl + len;
}

/* Sends a whole message. Waits at most MM_SEND_TIMEOUT_MS for socket buffer
 * space; a message sent only in part would corrupt the stream, so the link is
 * then marked failed and the agent reconnects. */
static bool link_send(mm_link_t *l, const uint8_t *buf, size_t len) {
  uint32_t t0 = mm_millis();
  while (len > 0) {
    int32_t n = mm_tcp_send(&l->tcp, buf, len > 0xFFFF ? 0xFFFF : (uint16_t)len);
    if (n < 0) break;
    if (n == 0) {
      if (mm_elapsed(t0) >= MM_SEND_TIMEOUT_MS) break;
      continue;
    }
    buf += n;
    len -= (size_t)n;
  }
  if (len == 0) return true;
  l->phase = MM_LINK_FAILED;
  return false;
}

static void append_text(char *dst, size_t cap, const uint8_t *src, size_t n) {
  size_t len = strlen(dst);
  if (len + n >= cap) n = cap - 1 - len;
  memcpy(dst + len, src, n);
  dst[len + n] = '\0';
}

/* ---------------------------------------------------------------------------
 * Received frames
 * ------------------------------------------------------------------------- */

/* REQ replies:
 *   settings: ["" delimiter][broker version][mads.ini text]
 *   timecode: ["" delimiter][timecode]   (no version frame) */
static void req_data(micromads_agent_t *a, uint8_t frame, const uint8_t *d, size_t n) {
  if (a->current_state == MM_STATE_REQ_WAIT_SETTINGS) {
    if (frame == 1) append_text(a->reply_version, sizeof(a->reply_version), d, n);
    else if (frame == 2) mm_ini_feed(&a->ini, d, n);
  } else if (frame == 1) {
    append_text(a->reply_text, sizeof(a->reply_text), d, n);
  }
}

/* SUB message: [topic][12-byte MADS header][JSON] */
static void sub_begin(micromads_agent_t *a, uint8_t frame) {
  if (frame == 0) {
    a->rx_topic[0] = '\0';
    a->rx_drop = 0;
  }
  a->rx_len = 0;
}

static void sub_data(micromads_agent_t *a, uint8_t frame, const uint8_t *d, size_t n) {
  if (a->rx_drop) return;
  if (frame == 0) {
    if (strlen(a->rx_topic) + n >= sizeof(a->rx_topic)) a->rx_drop = 1;
    else append_text(a->rx_topic, sizeof(a->rx_topic), d, n);
  } else if (frame == 1) {
    if (a->rx_len + n > sizeof(a->rx_header)) {
      a->rx_drop = 1; /* not a MADS header: e.g. snappy-compressed payload */
    } else {
      memcpy(a->rx_header + a->rx_len, d, n);
      a->rx_len = (uint16_t)(a->rx_len + n);
    }
  } else if (frame == 2) {
    if (a->rx_len + n >= sizeof(a->rx_buffer)) {
      a->rx_drop = 1; /* larger than MM_RX_BUF_SIZE */
    } else {
      memcpy(a->rx_buffer + a->rx_len, d, n);
      a->rx_len = (uint16_t)(a->rx_len + n);
    }
  } else {
    a->rx_drop = 1; /* blobs and other shapes are not supported */
  }
}

static void sub_end(micromads_agent_t *a, uint8_t frame, bool more) {
  if (frame == 1 && !a->rx_drop) {
    /* only uncompressed JSON (format 0, compression 0) */
    if (a->rx_len != sizeof(a->rx_header) || memcmp(a->rx_header, "MADS", 4) != 0 ||
        a->rx_header[5] != 0 || a->rx_header[6] != 0 || (a->rx_header[7] & 1))
      a->rx_drop = 1;
  }
  if (more) return;
  if (!a->rx_drop && frame == 2) {
    a->rx_buffer[a->rx_len] = '\0';
    a->received++;
    if (a->on_command_received) a->on_command_received(a->rx_topic, (const char *)a->rx_buffer);
  } else {
    a->dropped++;
  }
}

static void handle_event(micromads_agent_t *a, mm_link_t *l, const mm_zmtp_ev_t *ev) {
  switch (ev->type) {
  case MM_ZMTP_BEGIN:
    l->command = (ev->flags & MM_ZMTP_FLAG_COMMAND) != 0;
    l->cmd_len = 0;
    if (!l->command && l == &a->sub) sub_begin(a, l->frame);
    break;
  case MM_ZMTP_DATA:
    if (l->command) {
      size_t n = ev->len;
      if (n > sizeof(l->cmd) - l->cmd_len) n = sizeof(l->cmd) - l->cmd_len;
      memcpy(l->cmd + l->cmd_len, ev->data, n);
      l->cmd_len = (uint8_t)(l->cmd_len + n);
    } else if (l == &a->req) {
      req_data(a, l->frame, ev->data, ev->len);
    } else if (l == &a->sub) {
      sub_data(a, l->frame, ev->data, ev->len);
    } /* PUB: the broker forwards subscriptions to publishers; not needed */
    break;
  case MM_ZMTP_END:
    if (l->command) {
      if (l->cmd_len == 6 && memcmp(l->cmd, "\5READY", 6) == 0) {
        if (l->phase == MM_LINK_HANDSHAKE) l->phase = MM_LINK_UP;
      } else if (l->cmd_len == 6 && memcmp(l->cmd, "\5ERROR", 6) == 0) {
        l->phase = MM_LINK_FAILED;
        a->last_error = "broker rejected the ZMTP handshake";
      }
      break;
    }
    {
      bool more = (ev->flags & MM_ZMTP_FLAG_MORE) != 0;
      if (l == &a->req && !more) {
        a->reply_frames = (uint8_t)(l->frame + 1);
        a->reply_done = 1;
      } else if (l == &a->sub) {
        sub_end(a, l->frame, more);
      }
      /* saturates: frames past the ones we use must not wrap back to 0 */
      l->frame = more ? (uint8_t)(l->frame < 255 ? l->frame + 1 : 255) : 0;
    }
    break;
  case MM_ZMTP_ERROR:
    l->phase = MM_LINK_FAILED;
    a->last_error = "corrupt data from the broker";
    break;
  default:
    break;
  }
}

/* ---------------------------------------------------------------------------
 * Links
 * ------------------------------------------------------------------------- */

void mm_link_open(micromads_agent_t *a, mm_link_t *l, uint16_t port) {
  mm_zmtp_decoder_init(&l->dec);
  l->frame = 0;
  l->command = 0;
  l->t0 = mm_millis();
  if (mm_tcp_open(&l->tcp, a->broker_ip, port) == 0) {
    l->phase = MM_LINK_CONNECTING;
  } else {
    l->phase = MM_LINK_FAILED;
    a->last_error = "cannot open a TCP connection (check the broker IP)";
  }
}

void mm_link_close(mm_link_t *l) {
  mm_tcp_close(&l->tcp);
  l->phase = MM_LINK_CLOSED;
}

/* Advances connection and handshake, then reads and decodes what arrived. */
static void link_poll(micromads_agent_t *a, mm_link_t *l) {
  uint8_t chunk[128];
  uint32_t total = 0;

  if (l->phase == MM_LINK_CONNECTING) {
    int st = mm_tcp_status(&l->tcp);
    if (st == MM_TCP_UP) {
      /* our greeting and READY go out together; the broker answers with its
       * own greeting and READY (checked in handle_event) */
      uint8_t hello[MM_ZMTP_GREETING_SIZE + 32];
      size_t n = MM_ZMTP_GREETING_SIZE;
      memcpy(hello, mm_zmtp_greeting, n);
      n += mm_zmtp_ready(hello + n, l->type);
      if (link_send(l, hello, n)) l->phase = MM_LINK_HANDSHAKE;
    } else if (st < 0) {
      l->phase = MM_LINK_FAILED;
      a->last_error = "TCP connection refused (is the broker running?)";
    }
  }
  if ((l->phase == MM_LINK_CONNECTING || l->phase == MM_LINK_HANDSHAKE) &&
      mm_elapsed(l->t0) > MM_CONNECT_TIMEOUT_MS) {
    l->phase = MM_LINK_FAILED;
    a->last_error = "timeout connecting to the broker";
  }
  if (l->phase != MM_LINK_HANDSHAKE && l->phase != MM_LINK_UP) return;

  while (total < MAX_READ_PER_SPIN && (l->phase == MM_LINK_HANDSHAKE || l->phase == MM_LINK_UP)) {
    const uint8_t *p = chunk;
    int32_t n = mm_tcp_recv(&l->tcp, chunk, sizeof(chunk));
    if (n == 0) break;
    if (n < 0) {
      l->phase = MM_LINK_FAILED;
      a->last_error = "connection closed by the broker";
      break;
    }
    total += (uint32_t)n;
    for (;;) { /* until the decoder has consumed the chunk */
      mm_zmtp_ev_t ev;
      size_t used = mm_zmtp_decode(&l->dec, p, (size_t)n, &ev);
      p += used;
      n -= (int32_t)used;
      if (ev.type == MM_ZMTP_NONE) break;
      handle_event(a, l, &ev);
      if (ev.type == MM_ZMTP_ERROR) break;
    }
  }
}

/* ---------------------------------------------------------------------------
 * Requests and replies (used by the state machine)
 * ------------------------------------------------------------------------- */

static bool send_request(micromads_agent_t *a, const char *command, bool with_name) {
  /* A REQ message starts with an empty delimiter frame */
  builder_t b = {a->tx_buffer, sizeof(a->tx_buffer), 0, true};
  put_frame(&b, "", 0, true);
  put_frame(&b, MM_MADS_VERSION, strlen(MM_MADS_VERSION), true);
  put_frame(&b, command, strlen(command), with_name);
  if (with_name) put_frame(&b, a->name, strlen(a->name), false);
  if (!b.ok) return false;
  a->reply_done = 0;
  a->reply_frames = 0;
  a->reply_version[0] = '\0';
  a->reply_text[0] = '\0';
  a->request_tick = mm_millis();
  return link_send(&a->req, b.buf, b.len);
}

bool mm_request_settings(micromads_agent_t *a) {
  mm_ini_init(&a->ini, a->name);
  return send_request(a, "settings", true);
}

bool mm_request_timecode(micromads_agent_t *a) { return send_request(a, "timecode", false); }

bool mm_apply_settings(micromads_agent_t *a) {
  mm_ini_finish(&a->ini);
  if (a->reply_frames < 3) {
    /* the broker answers only with its version when it refuses ours */
    a->last_error = "broker refused MM_MADS_VERSION (its version is in reply_version)";
    return false;
  }
  a->pub_port = a->ini.frontend_port ? a->ini.frontend_port : 9090;
  a->sub_port = a->ini.backend_port ? a->ini.backend_port : 9091;
  a->timecode_fps = a->ini.timecode_fps ? a->ini.timecode_fps : 25;
  return true;
}

bool mm_apply_timecode(micromads_agent_t *a) {
  /* "33919.130000": seconds since the broker's local midnight */
  const char *s = a->reply_text;
  uint64_t sec = 0;
  uint32_t usec = 0, scale = 100000;
  if (*s < '0' || *s > '9') {
    a->last_error = "invalid timecode reply";
    return false;
  }
  for (; *s >= '0' && *s <= '9'; s++) sec = sec * 10 + (uint64_t)(*s - '0');
  if (*s == '.') {
    for (s++; *s >= '0' && *s <= '9' && scale > 0; s++, scale /= 10)
      usec += (uint32_t)(*s - '0') * scale;
  }
  /* The broker rounds its time down to a frame: take the middle of that
   * frame as our best estimate of the real time. */
  a->base_timecode_us = sec * 1000000u + usec + 500000u / a->timecode_fps;
  /* the broker read its clock about half a round trip after our request */
  a->base_tick = a->request_tick + mm_elapsed(a->request_tick) / 2;
  return true;
}

/* Broker timecode now, quantized to timecode_fps as MADS does. */
static void timecode_now(micromads_agent_t *a, uint32_t *sec, uint32_t *usec) {
  uint64_t us, frame;
  uint32_t elapsed = mm_elapsed(a->base_tick);
  if (elapsed > 3600000u) {
    /* move the base forward every hour, before millis() can wrap (49 days) */
    a->base_timecode_us = (a->base_timecode_us + (uint64_t)elapsed * 1000u) % 86400000000ULL;
    a->base_tick += elapsed;
    elapsed = 0;
  }
  us = a->base_timecode_us + (uint64_t)elapsed * 1000u;
  us %= 86400000000ULL; /* wraps at midnight */
  *sec = (uint32_t)(us / 1000000u);
  frame = (us % 1000000u) * a->timecode_fps / 1000000u;
  *usec = (uint32_t)(frame * 1000000u / a->timecode_fps);
}

bool mm_needs_sub(const micromads_agent_t *a) { return mm_agent_sub_topic_count(a) > 0; }

bool mm_send_subscriptions(micromads_agent_t *a) {
  uint8_t i;
  for (i = 0; i < mm_agent_sub_topic_count(a); i++) {
    /* ZMTP 3.0 subscription: a one-frame message [0x01][topic] */
    const char *topic = mm_agent_sub_topic(a, i);
    uint8_t body[1 + MM_MAX_TOPIC_LEN];
    size_t n = strlen(topic);
    builder_t b = {a->tx_buffer, sizeof(a->tx_buffer), 0, true};
    if (n >= MM_MAX_TOPIC_LEN) return false;
    body[0] = 1;
    memcpy(body + 1, topic, n);
    put_frame(&b, body, n + 1, false);
    if (!b.ok || !link_send(&a->sub, b.buf, b.len)) return false;
  }
  return true;
}

/* [topic][MADS header][JSON with agent_id, hostname, timecode added] */
static bool publish(micromads_agent_t *a, const char *topic, const char *json) {
  size_t tlen, rest, plen, hl, start, prefix;
  const size_t body_at = 2 + MM_MAX_TOPIC_LEN + 2 + sizeof(mads_header) + MM_ZMTP_HEADER_MAX;
  uint8_t *payload = a->tx_buffer + body_at;
  size_t cap = sizeof(a->tx_buffer) - body_at;
  uint8_t *p;
  uint32_t sec, usec;
  int n;

  if (a->pub.phase != MM_LINK_UP || !topic || !json) return false;
  tlen = strlen(topic);
  if (tlen >= MM_MAX_TOPIC_LEN) return false;
  while (*json == ' ' || *json == '\t' || *json == '\r' || *json == '\n') json++;
  if (*json != '{') return false;
  json++;
  while (*json == ' ' || *json == '\t' || *json == '\r' || *json == '\n') json++;

  timecode_now(a, &sec, &usec);
  n = snprintf((char *)payload, cap, "{\"agent_id\":\"%s\",\"hostname\":\"%s\",\"timecode\":%lu.%06lu%s",
               a->name, MM_HOSTNAME, (unsigned long)sec, (unsigned long)usec,
               *json == '}' ? "" : ",");
  rest = strlen(json); /* the rest of the user object, closing '}' included */
  if (n < 0 || (size_t)n + rest > cap) {
    a->dropped++;
    return false;
  }
  memcpy(payload + n, json, rest);
  plen = (size_t)n + rest;

  /* frame headers go right before the payload, so one send covers it all */
  {
    uint8_t hdr[MM_ZMTP_HEADER_MAX];
    hl = mm_zmtp_header(hdr, plen, 0);
    prefix = 2 + tlen + 2 + sizeof(mads_header) + hl;
    start = body_at - prefix;
    p = a->tx_buffer + start;
    p[0] = MM_ZMTP_FLAG_MORE;
    p[1] = (uint8_t)tlen;
    memcpy(p + 2, topic, tlen);
    p += 2 + tlen;
    p[0] = MM_ZMTP_FLAG_MORE;
    p[1] = sizeof(mads_header);
    memcpy(p + 2, mads_header, sizeof(mads_header));
    p += 2 + sizeof(mads_header);
    memcpy(p, hdr, hl);
  }
  if (!link_send(&a->pub, a->tx_buffer + start, prefix + plen)) {
    a->last_error = "publish failed, reconnecting";
    return false;
  }
  a->published++;
  return true;
}

void mm_publish_startup(micromads_agent_t *a) {
  char msg[256];
  uint8_t i;
  int n = snprintf(msg, sizeof(msg),
                   "{\"event\":\"startup\",\"name\":\"%s\",\"version\":\"%s\","
                   "\"settings\":{\"pub_topic\":\"%s\",\"sub_topic\":[",
                   a->name, MM_MADS_VERSION, mm_agent_pub_topic(a));
  for (i = 0; i < mm_agent_sub_topic_count(a) && n > 0 && (size_t)n < sizeof(msg); i++)
    n += snprintf(msg + n, sizeof(msg) - (size_t)n, "%s\"%s\"", i ? "," : "",
                  mm_agent_sub_topic(a, i));
  if (n > 0 && (size_t)n < sizeof(msg))
    n += snprintf(msg + n, sizeof(msg) - (size_t)n, "]}}");
  if (n > 0 && (size_t)n < sizeof(msg)) publish(a, "agent_event", msg);
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void mm_agent_init(micromads_agent_t *a, const char *name, const char *broker_ip,
                   uint16_t broker_port) {
  memset(a, 0, sizeof(*a));
  strncpy(a->name, name, sizeof(a->name) - 1);
  strncpy(a->broker_ip, broker_ip, sizeof(a->broker_ip) - 1);
  a->broker_port = broker_port;
  mm_tcp_init(&a->req.tcp, MM_LINK_REQ);
  mm_tcp_init(&a->pub.tcp, MM_LINK_PUB);
  mm_tcp_init(&a->sub.tcp, MM_LINK_SUB);
  a->req.type = "REQ";
  a->pub.type = "PUB";
  a->sub.type = "SUB";
  a->retry_ms = MM_RETRY_MIN_MS;
  a->retry_at = mm_millis();
  a->state_tick = mm_millis();
  a->current_state = MM_STATE_INIT;
}

void mm_agent_set_callback(micromads_agent_t *a, mm_command_cb_t callback) {
  a->on_command_received = callback;
}

void mm_agent_spin(micromads_agent_t *a) {
  mm_state_t prev = (mm_state_t)a->current_state;
  if (a->req.phase != MM_LINK_CLOSED) link_poll(a, &a->req);
  if (a->pub.phase != MM_LINK_CLOSED) link_poll(a, &a->pub);
  if (a->sub.phase != MM_LINK_CLOSED) link_poll(a, &a->sub);
  a->current_state = (int)mm_run_state(prev, a);
  if (a->current_state != (int)prev) a->state_tick = mm_millis();
}

bool mm_agent_is_ready(const micromads_agent_t *a) {
  return a->current_state == MM_STATE_READY;
}

bool mm_agent_publish(micromads_agent_t *a, const char *json_payload) {
  return mm_agent_publish_to(a, mm_agent_pub_topic(a), json_payload);
}

bool mm_agent_publish_to(micromads_agent_t *a, const char *topic, const char *json_payload) {
  if (!mm_agent_is_ready(a)) return false;
  return publish(a, topic, json_payload);
}

const char *mm_agent_pub_topic(const micromads_agent_t *a) {
#ifdef PUB_TOPIC
  (void)a;
  return PUB_TOPIC;
#else
  /* like a desktop agent: pub_topic from mads.ini, else the agent name */
  return a->ini.has_pub_topic ? a->ini.pub_topic : a->name;
#endif
}

uint8_t mm_agent_sub_topic_count(const micromads_agent_t *a) {
#ifdef SUB_TOPIC
  (void)a;
  return 1;
#else
  return a->ini.sub_topic_count;
#endif
}

const char *mm_agent_sub_topic(const micromads_agent_t *a, uint8_t i) {
#ifdef SUB_TOPIC
  (void)a;
  (void)i;
  return SUB_TOPIC;
#else
  return i < a->ini.sub_topic_count ? a->ini.sub_topics[i] : "";
#endif
}

const char *mm_agent_last_error(const micromads_agent_t *a) { return a->last_error; }

const char *mm_agent_state_name(const micromads_agent_t *a) {
  return mm_state_names[a->current_state];
}
