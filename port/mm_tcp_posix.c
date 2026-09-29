/*
 * mm_tcp over BSD sockets: ESP32 (lwIP sockets, Arduino or ESP-IDF) and
 * Linux/macOS for desktop testing.
 */
#include "mm_config.h"

#ifndef USE_W5500

#include "mm_tcp.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include "esp_timer.h"
#else
#include <sys/select.h>
#include <time.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static int would_block(int e) {
  return e == EAGAIN || e == EWOULDBLOCK || e == EINTR || e == ENOBUFS ||
         e == ENOMEM;
}

void mm_tcp_init(mm_tcp_t *s, uint8_t id) {
  (void)id;
  s->fd = -1;
  s->up = 0;
}

int mm_tcp_open(mm_tcp_t *s, const char *ip, uint16_t port) {
  struct sockaddr_in addr;
  int one = 1;

  mm_tcp_close(s);
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) return -1;

  s->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s->fd < 0) return -1;
  fcntl(s->fd, F_SETFL, fcntl(s->fd, F_GETFL, 0) | O_NONBLOCK);
  setsockopt(s->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  /* Keep-alive lets a silent SUB link notice a vanished broker: probe after
   * 10 s idle, every 5 s, give up after 3 (the default idle time is 2 h). */
  setsockopt(s->fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
#if defined(TCP_KEEPIDLE) && defined(TCP_KEEPINTVL) && defined(TCP_KEEPCNT)
  {
    int idle = 10, intvl = 5, cnt = 3;
    setsockopt(s->fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(s->fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(s->fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
  }
#endif
#ifdef SO_NOSIGPIPE
  setsockopt(s->fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

  if (connect(s->fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
    s->up = 1;
    return 0;
  }
  if (errno == EINPROGRESS) return 0;
  mm_tcp_close(s);
  return -1;
}

int mm_tcp_status(mm_tcp_t *s) {
  fd_set wfds;
  struct timeval tv = {0, 0};
  int err = 0;
  socklen_t len = sizeof(err);

  if (s->fd < 0) return -1;
  if (s->up) return MM_TCP_UP;

  /* Zero-timeout poll: writable means connect() has finished. */
  FD_ZERO(&wfds);
  FD_SET(s->fd, &wfds);
  if (select(s->fd + 1, NULL, &wfds, NULL, &tv) <= 0) return MM_TCP_CONNECTING;
  if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0)
    return -1;
  s->up = 1;
  return MM_TCP_UP;
}

int32_t mm_tcp_send(mm_tcp_t *s, const uint8_t *buf, uint16_t len) {
  ssize_t n;
  if (s->fd < 0 || !s->up) return -1;
  n = send(s->fd, buf, len, MSG_DONTWAIT | MSG_NOSIGNAL);
  if (n >= 0) return (int32_t)n;
  return would_block(errno) ? 0 : -1;
}

int32_t mm_tcp_recv(mm_tcp_t *s, uint8_t *buf, uint16_t cap) {
  ssize_t n;
  if (s->fd < 0 || !s->up) return -1;
  n = recv(s->fd, buf, cap, MSG_DONTWAIT);
  if (n > 0) return (int32_t)n;
  if (n == 0) return -1; /* orderly close by the peer */
  return would_block(errno) ? 0 : -1;
}

void mm_tcp_close(mm_tcp_t *s) {
  if (s->fd >= 0) close(s->fd);
  s->fd = -1;
  s->up = 0;
}

uint32_t mm_millis(void) {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
  return (uint32_t)(esp_timer_get_time() / 1000);
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
#endif
}

#else
typedef int mm_tcp_posix_unused; /* keeps the translation unit non-empty */
#endif /* !USE_W5500 */
