#include "mm_zmtp.h"
#include <string.h>

/* signature (10) | version 3.0 (2) | mechanism "NULL" (20) | as-server (1) | filler (31) */
const uint8_t mm_zmtp_greeting[MM_ZMTP_GREETING_SIZE] = {
    0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0x7F, 3, 0, 'N', 'U', 'L', 'L'};

size_t mm_zmtp_header(uint8_t out[MM_ZMTP_HEADER_MAX], uint64_t len, uint8_t flags) {
  int i;
  if (len <= 255) {
    out[0] = flags;
    out[1] = (uint8_t)len;
    return 2;
  }
  out[0] = (uint8_t)(flags | MM_ZMTP_FLAG_LONG);
  for (i = 0; i < 8; i++) out[1 + i] = (uint8_t)(len >> (8 * (7 - i)));
  return 9;
}

size_t mm_zmtp_ready(uint8_t *out, const char *socket_type) {
  /* body: [5]"READY" [11]"Socket-Type" [4-byte length] value */
  size_t vlen = strlen(socket_type);
  size_t n = 2;
  out[n++] = 5;
  memcpy(out + n, "READY", 5);
  n += 5;
  out[n++] = 11;
  memcpy(out + n, "Socket-Type", 11);
  n += 11;
  out[n++] = 0;
  out[n++] = 0;
  out[n++] = 0;
  out[n++] = (uint8_t)vlen;
  memcpy(out + n, socket_type, vlen);
  n += vlen;
  out[0] = MM_ZMTP_FLAG_COMMAND;
  out[1] = (uint8_t)(n - 2);
  return n;
}

enum { S_GREETING = 0, S_FLAGS, S_LENGTH, S_BODY, S_END, S_ERROR };

void mm_zmtp_decoder_init(mm_zmtp_decoder_t *d) { memset(d, 0, sizeof(*d)); }

/* Checks byte `i` of the peer greeting. */
static int greeting_byte_ok(uint8_t i, uint8_t b) {
  if (i == 0) return b == 0xFF;
  if (i == 9) return (b & 0x01) != 0;
  if (i == 10) return b >= 3; /* ZMTP 3.x */
  if (i >= 12 && i < 16) return b == (uint8_t)"NULL"[i - 12];
  if (i >= 16 && i < 32) return b == 0;
  return 1;
}

size_t mm_zmtp_decode(mm_zmtp_decoder_t *d, const uint8_t *in, size_t n,
                      mm_zmtp_ev_t *ev) {
  size_t used = 0;

  for (;;) {
    ev->type = MM_ZMTP_NONE;
    ev->flags = d->flags;
    ev->size = d->size;

    if (d->state == S_ERROR) {
      ev->type = MM_ZMTP_ERROR;
      return used;
    }
    if (d->state == S_END) {
      d->state = S_FLAGS;
      ev->type = MM_ZMTP_END;
      return used;
    }
    if (d->state == S_BODY) {
      size_t chunk;
      if (d->left == 0) {
        d->state = S_END;
        continue;
      }
      if (used >= n) return used;
      chunk = n - used;
      if ((uint64_t)chunk > d->left) chunk = (size_t)d->left;
      ev->type = MM_ZMTP_DATA;
      ev->data = in + used;
      ev->len = chunk;
      d->left -= chunk;
      return used + chunk;
    }
    if (used >= n) return used;

    {
      uint8_t b = in[used++];
      switch (d->state) {
      case S_GREETING:
        if (!greeting_byte_ok(d->count, b)) {
          d->state = S_ERROR;
          continue;
        }
        if (++d->count == MM_ZMTP_GREETING_SIZE) {
          d->state = S_FLAGS;
          ev->type = MM_ZMTP_GREETING;
          return used;
        }
        break;
      case S_FLAGS:
        if (b & 0xF8) { /* reserved bits */
          d->state = S_ERROR;
          continue;
        }
        d->flags = b;
        d->size = 0;
        d->count = 0;
        d->state = S_LENGTH;
        break;
      default: /* S_LENGTH */
        d->size = (d->size << 8) | b;
        if (++d->count == ((d->flags & MM_ZMTP_FLAG_LONG) ? 8 : 1)) {
          if (d->size >> 63) {
            d->state = S_ERROR;
            continue;
          }
          d->left = d->size;
          d->state = S_BODY;
          ev->type = MM_ZMTP_BEGIN;
          ev->flags = d->flags;
          ev->size = d->size;
          return used;
        }
        break;
      }
    }
  }
}
