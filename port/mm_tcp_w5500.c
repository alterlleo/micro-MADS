/*
 * mm_tcp over a WIZnet W5500, using the WIZnet ioLibrary_Driver socket API in
 * non-blocking mode (SF_IO_NONBLOCK). The application initialises the chip
 * (SPI callbacks, wizchip_init, IP/MAC) before starting the agent.
 */
#include "mm_config.h"

#ifdef USE_W5500

#include "mm_tcp.h"

#include <stdlib.h>

#include "socket.h"
#include "wizchip_conf.h"

#ifndef MM_GET_TICK
extern uint32_t HAL_GetTick(void);
#define MM_GET_TICK() HAL_GetTick()
#endif

#define KEEPALIVE_5S_UNITS 2 /* keep-alive probe every 10 s when idle */

static int parse_ip(const char *s, uint8_t ip[4]) {
  int i;
  for (i = 0; i < 4; i++) {
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s || v < 0 || v > 255) return -1;
    ip[i] = (uint8_t)v;
    if (i < 3) {
      if (*end != '.') return -1;
      s = end + 1;
    } else if (*end != '\0') {
      return -1;
    }
  }
  return 0;
}

void mm_tcp_init(mm_tcp_t *s, uint8_t id) {
  static const uint8_t sockets[3] = {MM_W5500_SOCK_REQ, MM_W5500_SOCK_PUB,
                                     MM_W5500_SOCK_SUB};
  s->sn = sockets[id < 3 ? id : 0];
  s->open = 0;
}

int mm_tcp_open(mm_tcp_t *s, const char *ip, uint16_t port) {
  uint8_t addr[4];
  uint8_t keepalive = KEEPALIVE_5S_UNITS;
  int8_t r;

  if (parse_ip(ip, addr) != 0) return -1;
  /* socket() closes the hardware socket first if it was still in use. */
  if (socket(s->sn, Sn_MR_TCP, 0, SF_IO_NONBLOCK) != (int8_t)s->sn) return -1;
  s->open = 1;
  setsockopt(s->sn, SO_KEEPALIVEAUTO, &keepalive);
  r = connect(s->sn, addr, port);
  if (r != SOCK_OK && r != SOCK_BUSY) {
    mm_tcp_close(s);
    return -1;
  }
  return 0;
}

int mm_tcp_status(mm_tcp_t *s) {
  if (!s->open) return -1;
  switch (getSn_SR(s->sn)) {
  case SOCK_ESTABLISHED:
    return MM_TCP_UP;
  case SOCK_INIT:
  case SOCK_SYNSENT:
    return MM_TCP_CONNECTING;
  default: /* SOCK_CLOSED (refused/timeout), SOCK_CLOSE_WAIT (peer closed) */
    return -1;
  }
}

int32_t mm_tcp_send(mm_tcp_t *s, const uint8_t *buf, uint16_t len) {
  uint16_t room;
  int32_t r;

  if (mm_tcp_status(s) != MM_TCP_UP) return -1;
  /* In non-blocking mode send() refuses a block larger than the free TX
   * memory, so offer only what fits. */
  room = getSn_TX_FSR(s->sn);
  if (room == 0) return 0;
  if (len > room) len = room;
  r = send(s->sn, (uint8_t *)buf, len); /* ioLibrary takes a non-const buffer */
  if (r == SOCK_BUSY) return 0;
  return r > 0 ? r : -1;
}

int32_t mm_tcp_recv(mm_tcp_t *s, uint8_t *buf, uint16_t cap) {
  uint16_t n;
  uint8_t sr;

  if (!s->open) return -1;
  sr = getSn_SR(s->sn);
  if (sr != SOCK_ESTABLISHED && sr != SOCK_CLOSE_WAIT) return -1;
  n = getSn_RX_RSR(s->sn);
  if (n == 0) return sr == SOCK_CLOSE_WAIT ? -1 : 0;
  if (n > cap) n = cap;
  /* Read the RX buffer directly: in non-blocking mode, recv() of recent
   * ioLibrary versions returns SOCK_BUSY on the W5500 even when data is
   * waiting. This is what recv() does once it decides to read. */
  wiz_recv_data(s->sn, buf, n);
  setSn_CR(s->sn, Sn_CR_RECV);
  while (getSn_CR(s->sn)) {
    /* the chip accepts the command within a few SPI cycles */
  }
  return n;
}

void mm_tcp_close(mm_tcp_t *s) {
  if (!s->open) return;
  /* Graceful: the chip sends what is still queued, then FIN. disconnect()
   * returns at once in non-blocking mode. A later socket() on the same
   * number closes it for good if it is still lingering. */
  if (getSn_SR(s->sn) == SOCK_ESTABLISHED) {
    disconnect(s->sn);
  } else {
    close(s->sn);
  }
  s->open = 0;
}

uint32_t mm_millis(void) { return (uint32_t)MM_GET_TICK(); }

#else
typedef int mm_tcp_w5500_unused; /* keeps the translation unit non-empty */
#endif /* USE_W5500 */
