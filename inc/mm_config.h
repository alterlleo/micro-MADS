/*
 * micro-MADS configuration. Every value can be overridden with a compiler flag
 * (e.g. -DMM_RX_BUF_SIZE=1024). Library and application must be compiled with
 * the same values, because they change the size of micromads_agent_t.
 */
#ifndef MM_CONFIG_H
#define MM_CONFIG_H

/* ---- Transport: exactly one of them ------------------------------------
 * USE_ESP32 : BSD sockets of the ESP32 (Wi-Fi or Ethernet)
 * USE_W5500 : WIZnet W5500 through the WIZnet ioLibrary_Driver
 * On a desktop (Linux/macOS) the BSD socket port is used, for testing. */
#if defined(USE_ESP32) && defined(USE_W5500)
#error "Define only one of USE_ESP32 and USE_W5500"
#endif

/* W5500 hardware sockets reserved for micro-MADS (0..7) */
#ifndef MM_W5500_SOCK_REQ
#define MM_W5500_SOCK_REQ 0
#endif
#ifndef MM_W5500_SOCK_PUB
#define MM_W5500_SOCK_PUB 1
#endif
#ifndef MM_W5500_SOCK_SUB
#define MM_W5500_SOCK_SUB 2
#endif

/* ---- Protocol ------------------------------------------------------------
 * The broker only accepts agents with its own "vMAJOR.MINOR" (v2.4.x). */
#ifndef MM_MADS_VERSION
#define MM_MADS_VERSION "v2.4.3"
#endif

/* Optional overrides of the topics read from mads.ini, e.g.
 * -DPUB_TOPIC=\"telemetry\" -DSUB_TOPIC=\"commands\" (one topic only) */
/* #define PUB_TOPIC "telemetry" */
/* #define SUB_TOPIC "commands" */

/* ---- Sizes (strings include the terminating NUL) ------------------------- */
#ifndef MM_MAX_NAME_LEN
#define MM_MAX_NAME_LEN 32
#endif
#ifndef MM_MAX_IP_LEN
#define MM_MAX_IP_LEN 16
#endif
#ifndef MM_MAX_TOPIC_LEN
#define MM_MAX_TOPIC_LEN 32
#endif
#ifndef MM_MAX_SUB_TOPICS
#define MM_MAX_SUB_TOPICS 4
#endif
#ifndef MM_INI_LINE_LEN
#define MM_INI_LINE_LEN 128 /* longer mads.ini lines are skipped */
#endif
#ifndef MM_RX_BUF_SIZE
#define MM_RX_BUF_SIZE 512 /* largest received JSON payload */
#endif
#ifndef MM_TX_BUF_SIZE
#define MM_TX_BUF_SIZE 512 /* largest published message (JSON + envelope) */
#endif

/* ---- Timing (milliseconds) ---------------------------------------------- */
#ifndef MM_CONNECT_TIMEOUT_MS
#define MM_CONNECT_TIMEOUT_MS 3000 /* TCP connect + ZMTP handshake */
#endif
#ifndef MM_REPLY_TIMEOUT_MS
#define MM_REPLY_TIMEOUT_MS 3000 /* settings / timecode replies */
#endif
#ifndef MM_SEND_TIMEOUT_MS
#define MM_SEND_TIMEOUT_MS 100 /* max wait for socket buffer space */
#endif
#ifndef MM_RETRY_MIN_MS
#define MM_RETRY_MIN_MS 1000 /* first reconnect delay, doubles on failure */
#endif
#ifndef MM_RETRY_MAX_MS
#define MM_RETRY_MAX_MS 30000
#endif

/* ---- Time source -------------------------------------------------------
 * ESP32/desktop: provided by the socket port. W5500: HAL_GetTick() by
 * default (STM32 HAL); define MM_GET_TICK() for other platforms. */

#endif /* MM_CONFIG_H */
