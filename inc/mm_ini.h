/*
 * Streaming reader for the mads.ini text sent by the broker.
 *
 * The broker sends the WHOLE mads.ini (several KB) to every agent, so the
 * text is fed chunk by chunk as it arrives and processed line by line in a
 * small buffer: memory use does not depend on the file size. Only what an
 * agent needs is kept:
 *   [agents]   frontend_address, backend_address (the port), timecode_fps
 *   [<name>]   pub_topic (string), sub_topic (string or array of strings)
 */
#ifndef MM_INI_H
#define MM_INI_H

#include <stddef.h>
#include <stdint.h>

#include "mm_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  /* results (0 / empty when not found) */
  uint16_t frontend_port;
  uint16_t backend_port;
  uint16_t timecode_fps;
  uint8_t section_found; /* [<name>] exists */
  uint8_t has_pub_topic;
  uint8_t has_sub_topic;
  uint8_t sub_topic_count;
  char pub_topic[MM_MAX_TOPIC_LEN];
  char sub_topics[MM_MAX_SUB_TOPICS][MM_MAX_TOPIC_LEN];

  /* private */
  const char *section;
  uint8_t table;    /* 0 other, 1 [agents], 2 [<name>] */
  uint8_t skip;     /* dropping the rest of an overlong line */
  uint8_t in_array; /* inside a multi-line sub_topic array */
  uint16_t len;
  char line[MM_INI_LINE_LEN];
} mm_ini_t;

/* `section` (the agent name) must stay valid until mm_ini_finish(). */
void mm_ini_init(mm_ini_t *ini, const char *section);
void mm_ini_feed(mm_ini_t *ini, const uint8_t *data, size_t len);
void mm_ini_finish(mm_ini_t *ini); /* processes a last line without '\n' */

#ifdef __cplusplus
}
#endif

#endif /* MM_INI_H */
