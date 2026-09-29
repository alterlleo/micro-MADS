/*
 * ZMTP 3.0 framing (NULL security), the subset MADS needs.
 *
 * TCP is a byte stream: one recv() may hold half a frame or several frames.
 * The decoder is therefore fed whatever bytes arrived and reports frame
 * boundaries as events; body bytes are handed out in chunks pointing into the
 * caller's buffer, so frames of any size are never copied whole.
 */
#ifndef MM_ZMTP_H
#define MM_ZMTP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MM_ZMTP_FLAG_MORE 0x01
#define MM_ZMTP_FLAG_LONG 0x02
#define MM_ZMTP_FLAG_COMMAND 0x04

#define MM_ZMTP_GREETING_SIZE 64
#define MM_ZMTP_HEADER_MAX 9

/* ---- Encoding ---------------------------------------------------------- */

/* Our greeting: ZMTP 3.0, mechanism NULL, client. Minor version 0 makes
 * libzmq use plain [0x01][topic] subscription frames and no heartbeats. */
extern const uint8_t mm_zmtp_greeting[MM_ZMTP_GREETING_SIZE];

/* Frame header for a body of len bytes (flags: MORE and/or COMMAND). Uses
 * the 8-byte length form above 255 bytes. Returns 2 or 9. */
size_t mm_zmtp_header(uint8_t out[MM_ZMTP_HEADER_MAX], uint64_t len, uint8_t flags);

/* Complete READY command (header included) for "REQ", "PUB" or "SUB".
 * out must hold 64 bytes. Returns its length. */
size_t mm_zmtp_ready(uint8_t *out, const char *socket_type);

/* ---- Decoding ---------------------------------------------------------- */

typedef enum {
  MM_ZMTP_NONE = 0,  /* all input consumed, feed more */
  MM_ZMTP_GREETING,  /* the peer greeting arrived and is valid */
  MM_ZMTP_BEGIN,     /* new frame: flags, size */
  MM_ZMTP_DATA,      /* data[0..len) is the next piece of the frame body */
  MM_ZMTP_END,       /* the frame body is complete */
  MM_ZMTP_ERROR      /* not a ZMTP 3 / NULL peer, or corrupt stream */
} mm_zmtp_event_t;

typedef struct {
  mm_zmtp_event_t type;
  uint8_t flags;       /* flags of the current frame */
  uint64_t size;       /* body size of the current frame */
  const uint8_t *data; /* MM_ZMTP_DATA only */
  size_t len;          /* MM_ZMTP_DATA only */
} mm_zmtp_ev_t;

typedef struct {
  uint8_t state;
  uint8_t count; /* greeting / length bytes seen */
  uint8_t flags;
  uint64_t size;
  uint64_t left; /* body bytes still to come */
} mm_zmtp_decoder_t;

void mm_zmtp_decoder_init(mm_zmtp_decoder_t *d);

/* Consumes input until one event is produced. Returns the bytes consumed.
 * Call it in a loop until it reports MM_ZMTP_NONE (an event can come with 0
 * bytes consumed, e.g. END of an empty frame). After MM_ZMTP_ERROR the
 * connection must be closed. */
size_t mm_zmtp_decode(mm_zmtp_decoder_t *d, const uint8_t *in, size_t n,
                      mm_zmtp_ev_t *ev);

#ifdef __cplusplus
}
#endif

#endif /* MM_ZMTP_H */
