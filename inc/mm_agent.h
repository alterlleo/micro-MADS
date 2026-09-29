/*
 * micro-MADS agent API.
 *
 * The agent runs the MADS start-up sequence on its own, driven by
 * mm_agent_spin() (never blocks):
 *   1. fetch mads.ini from the broker settings port (ports, pub/sub topics);
 *   2. fetch the broker timecode;
 *   3. connect PUB and SUB, subscribe, publish the "startup" agent_event;
 *   4. ready: publish / receive; on a lost connection, reconnect with a
 *      growing delay (1 s, 2 s, 4 s ... 30 s).
 *
 * Messages are JSON text built by the application. Received messages are
 * delivered as JSON text; this requires the sender to publish uncompressed
 * JSON, which MADS agents do for messages under 256 bytes (default
 * compression = "auto"). Set compression = "none" (and wire_format = "json")
 * in the sender's mads.ini section for larger messages.
 */
#ifndef MM_AGENT_H
#define MM_AGENT_H

#include <stdbool.h>
#include <stdint.h>

#include "mm_config.h"
#include "mm_ini.h"
#include "mm_tcp.h"
#include "mm_zmtp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called for every message received on a subscribed topic. payload is the
 * JSON text, NUL-terminated. */
typedef void (*mm_command_cb_t)(const char *topic, const char *payload);

/* One ZMTP connection to the broker (internal). */
typedef struct {
  mm_tcp_t tcp;
  mm_zmtp_decoder_t dec;
  const char *type; /* "REQ", "PUB", "SUB" */
  uint8_t phase;    /* closed / connecting / handshake / up / failed */
  uint8_t frame;    /* index of the current frame in its message */
  uint8_t command;  /* the current frame is a ZMTP command */
  uint8_t cmd[6];   /* first bytes of the command body ("\5READY") */
  uint8_t cmd_len;
  uint32_t t0;      /* when the current phase started (ms) */
} mm_link_t;

typedef struct {
  char name[MM_MAX_NAME_LEN];
  char broker_ip[MM_MAX_IP_LEN];
  uint16_t broker_port; /* settings port, 9092 */

  /* settings from mads.ini */
  mm_ini_t ini;
  uint16_t pub_port, sub_port, timecode_fps;

  /* connections */
  mm_link_t req, pub, sub;

  /* state machine */
  int current_state; /* MM_STATE_* from mm_fsm.h */
  uint32_t state_tick; /* when the current state was entered (ms) */
  uint32_t retry_at, retry_ms;

  /* reply on the REQ connection */
  uint8_t reply_done, reply_frames;
  char reply_version[16];
  char reply_text[24];
  uint32_t request_tick;

  /* broker timecode: base_timecode_us at local time base_tick */
  uint64_t base_timecode_us;
  uint32_t base_tick;

  /* message being received on SUB */
  char rx_topic[MM_MAX_TOPIC_LEN];
  uint8_t rx_header[12];
  uint16_t rx_len;
  uint8_t rx_drop;
  uint8_t rx_buffer[MM_RX_BUF_SIZE];

  uint8_t tx_buffer[MM_TX_BUF_SIZE];

  mm_command_cb_t on_command_received;
  const char *last_error;
  uint32_t published, received, dropped;
} micromads_agent_t;

/* broker_port is the settings port (9092 in the default mads.ini). */
void mm_agent_init(micromads_agent_t *agent, const char *name, const char *broker_ip,
                   uint16_t broker_port);

void mm_agent_set_callback(micromads_agent_t *agent, mm_command_cb_t callback);

/* Call it continuously from the main loop. */
void mm_agent_spin(micromads_agent_t *agent);

bool mm_agent_is_ready(const micromads_agent_t *agent);

/* Publishes a JSON object ("{...}") on the agent's pub_topic, or on `topic`.
 * agent_id, hostname and the broker timecode are added to it. Returns false if
 * the agent is not ready, the text is not an object, or it is too long for
 * MM_TX_BUF_SIZE. */
bool mm_agent_publish(micromads_agent_t *agent, const char *json_payload);
bool mm_agent_publish_to(micromads_agent_t *agent, const char *topic,
                         const char *json_payload);

/* Topics in use (from mads.ini unless PUB_TOPIC/SUB_TOPIC are defined). */
const char *mm_agent_pub_topic(const micromads_agent_t *agent);
uint8_t mm_agent_sub_topic_count(const micromads_agent_t *agent);
const char *mm_agent_sub_topic(const micromads_agent_t *agent, uint8_t i);

/* Human-readable reason of the last failure (NULL if none). */
const char *mm_agent_last_error(const micromads_agent_t *agent);

/* Name of the current state, e.g. "ready". */
const char *mm_agent_state_name(const micromads_agent_t *agent);

#ifdef __cplusplus
}
#endif

#endif /* MM_AGENT_H */
