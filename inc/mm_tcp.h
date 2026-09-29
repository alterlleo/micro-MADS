/*
 * Non-blocking TCP client, the only platform-specific part of micro-MADS.
 * Implemented by port/mm_tcp_posix.c (ESP32, desktop) and
 * port/mm_tcp_w5500.c (W5500). No function ever blocks.
 */
#ifndef MM_TCP_H
#define MM_TCP_H

#include <stdint.h>

#include "mm_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MM_TCP_CONNECTING 0 /* connection in progress */
#define MM_TCP_UP 1         /* connected */
/* negative values: failed, refused or closed by the peer */

typedef struct {
#ifdef USE_W5500
  uint8_t sn;   /* W5500 hardware socket */
  uint8_t open; /* socket() done */
#else
  int fd; /* -1 when closed */
  uint8_t up;
#endif
} mm_tcp_t;

/* Prepares a closed handle. `id` is 0 (REQ), 1 (PUB) or 2 (SUB): it selects
 * the W5500 hardware socket and is ignored by the socket port. */
void mm_tcp_init(mm_tcp_t *s, uint8_t id);

/* Starts connecting to ip:port. Returns 0 if started, <0 on error. */
int mm_tcp_open(mm_tcp_t *s, const char *ip, uint16_t port);

/* MM_TCP_CONNECTING, MM_TCP_UP, or <0 if failed/closed. */
int mm_tcp_status(mm_tcp_t *s);

/* Sends up to len bytes. Returns bytes accepted (0 = buffer full, retry
 * later) or <0 if the connection is broken. */
int32_t mm_tcp_send(mm_tcp_t *s, const uint8_t *buf, uint16_t len);

/* Reads up to cap bytes. Returns bytes read, 0 if nothing is available, or
 * <0 if the peer closed the connection or it failed. */
int32_t mm_tcp_recv(mm_tcp_t *s, uint8_t *buf, uint16_t cap);

/* Closes the connection. Safe to call on a closed handle. */
void mm_tcp_close(mm_tcp_t *s);

/* Monotonic milliseconds (wraps after 49 days, always compare differences). */
uint32_t mm_millis(void);

#ifdef __cplusplus
}
#endif

#endif /* MM_TCP_H */
