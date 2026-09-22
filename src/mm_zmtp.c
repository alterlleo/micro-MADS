#include "mm_zmtp.h"
#include <string.h>
#include <stdio.h>

// #define USE_W5500 

#ifdef USE_W5500 
  #include "wizchip_conf.h"
  #include "socket.h"
#elif defined(USE_ESP32)
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <netinet/tcp.h>
#else
  #include "lwip/tcp.h"
  #include "lwip/inet.h"
  #include "lwip/pbuf.h"
#endif

#define MM_LIB_VERSION "v2.4.3"

static const uint8_t ZMTP_GREETING[64] = {
    0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 
    0x03, 0x00,                                                 
    'N', 'U', 'L', 'L', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0x00, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const uint8_t ZMTP_READY_REQ[27] = {
    0x04, 25, 0x05, 'R','E','A','D','Y',
    0x0B, 'S','o','c','k','e','t','-','T','y','p','e',
    0x00, 0x00, 0x00, 0x03, 'R','E','Q'
};

static const uint8_t ZMTP_READY_PUB[27] = {
    0x04, 25, 0x05, 'R','E','A','D','Y',
    0x0B, 'S','o','c','k','e','t','-','T','y','p','e',
    0x00, 0x00, 0x00, 0x03, 'P','U','B'
};

static const uint8_t ZMTP_READY_SUB[27] = {
    0x04, 25, 0x05, 'R','E','A','D','Y',
    0x0B, 'S','o','c','k','e','t','-','T','y','p','e',
    0x00, 0x00, 0x00, 0x03, 'S','U','B'
};

static bool send_zmtp_frame(void *pcb_ptr, const char *data, uint8_t len, bool more) {
    uint8_t header[2];
    header[0] = more ? 0x01 : 0x00; 
    header[1] = len;

  #ifdef USE_W5500
    uint8_t socket_num = (uint8_t)((uintptr_t)pcb_ptr);
    if (send(socket_num, header, 2) <= 0) return false;
    if (send(socket_num, (uint8_t*)data, len) <= 0) return false;    
    return true;
  #elif defined(USE_ESP32)
    int sock = (int)((uintptr_t)pcb_ptr);
    if (send(sock, header, 2, 0) < 0) return false;
    if (len > 0) {
        if (send(sock, data, len, 0) < 0) return false;
    }
    return true;
  #else
    struct tcp_pcb *pcb = (struct tcp_pcb *)pcb_ptr;
    if (tcp_write(pcb, header, 2, TCP_WRITE_FLAG_COPY) != ERR_OK) return false;
    if (tcp_write(pcb, data, len, TCP_WRITE_FLAG_COPY) != ERR_OK) return false;
    return true;
  #endif
}

bool mm_zmtp_send_greeting(void *pcb_ptr) {
    if (!pcb_ptr) return false;
  #ifdef USE_W5500
    uint8_t socket_num = (uint8_t)((uintptr_t)pcb_ptr);
    return (send(socket_num, (uint8_t*)ZMTP_GREETING, sizeof(ZMTP_GREETING)) > 0);
  #elif defined(USE_ESP32)
    int sock = (int)((uintptr_t)pcb_ptr);
    return (send(sock, ZMTP_GREETING, sizeof(ZMTP_GREETING), 0) > 0);
  #else
    struct tcp_pcb *pcb = (struct tcp_pcb *)pcb_ptr;
    err_t err = tcp_write(pcb, ZMTP_GREETING, sizeof(ZMTP_GREETING), TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) tcp_output(pcb);
    return (err == ERR_OK);
  #endif
}

bool mm_zmtp_send_ready(void *pcb_ptr, mm_zmq_socket_type_t socket_type) {
    if (!pcb_ptr) return false;
    const uint8_t *ready_frame;
    if (socket_type == MM_ZMQ_SOCKET_REQ) ready_frame = ZMTP_READY_REQ;
    else if (socket_type == MM_ZMQ_SOCKET_PUB) ready_frame = ZMTP_READY_PUB;
    else ready_frame = ZMTP_READY_SUB;

  #ifdef USE_W5500
    uint8_t socket_num = (uint8_t)((uintptr_t)pcb_ptr);
    return (send(socket_num, (uint8_t*)ready_frame, 27) > 0);
  #elif defined(USE_ESP32)
    int sock = (int)((uintptr_t)pcb_ptr);
    return (send(sock, ready_frame, 27, 0) > 0);
  #else
    struct tcp_pcb *pcb = (struct tcp_pcb *)pcb_ptr;
    err_t err = tcp_write(pcb, ready_frame, 27, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) tcp_output(pcb);
    return (err == ERR_OK);
  #endif
}

#if !defined(USE_W5500) && !defined(USE_ESP32)
static err_t mm_tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
  micromads_agent_t *agent = (micromads_agent_t *)arg;
  if (p == NULL) {
    if (tpcb == agent->req_pcb) agent->req_pcb = NULL;
    if (tpcb == agent->pub_pcb) agent->pub_pcb = NULL;
    if (tpcb == agent->sub_pcb) agent->sub_pcb = NULL;
    tcp_close(tpcb);
    return ERR_OK;
  }
  if (err == ERR_OK) {
    if (tpcb == agent->req_pcb) {
      u16_t copy_len = p->tot_len;
      if (copy_len > (MM_MAX_PAYLOAD_LEN - agent->rx_index - 1)) {
        copy_len = MM_MAX_PAYLOAD_LEN - agent->rx_index - 1;
      }
      pbuf_copy_partial(p, agent->rx_buffer + agent->rx_index, copy_len, 0);
      agent->rx_index += copy_len;
      agent->rx_buffer[agent->rx_index] = '\0';
    } else if (tpcb == agent->sub_pcb) {
      uint8_t *data = (uint8_t *)p->payload;
      uint16_t len = p->tot_len;
      if (len > 4) {
        uint8_t flags1 = data[0];
        uint8_t len1 = data[1];
        if (flags1 == 0x01 && (2 + len1 + 2) < len) {
          char *rx_topic = (char *)&data[2];
          uint8_t flags2 = data[2 + len1];
          uint8_t len2 = data[2 + len1 + 1];
          if (flags2 == 0x00 && (2 + len1 + 2 + len2) <= len) {
            char *rx_payload = (char *)&data[2 + len1 + 2];
            char backup_t = data[2 + len1]; 
            char backup_p = data[2 + len1 + 2 + len2];
            data[2 + len1] = '\0';
            if ((2 + len1 + 2 + len2) < len) data[2 + len1 + 2 + len2] = '\0';
            if (agent->on_command_received != NULL) {
              agent->on_command_received(rx_topic, rx_payload);
            }
            data[2 + len1] = backup_t;
            if ((2 + len1 + 2 + len2) < len) data[2 + len1 + 2 + len2] = backup_p;
          }
        }
      }
    }
    tcp_recved(tpcb, p -> tot_len);
  }
  pbuf_free(p);
  return ERR_OK;
}

static void mm_tcp_error_callback(void *arg, err_t err) {
  micromads_agent_t *agent = (micromads_agent_t *)arg;
  if (agent) { agent -> req_pcb = NULL; }
}

static err_t mm_tcp_connect_callback(void *arg, struct tcp_pcb *tpcb, err_t err) {
  micromads_agent_t *agent = (micromads_agent_t *)arg;
  if (err != ERR_OK) return err;
  mm_zmtp_send_greeting(tpcb);
  if (tpcb == agent->req_pcb) {
    mm_zmtp_send_ready(tpcb, MM_ZMQ_SOCKET_REQ);
  } else if (tpcb == agent->pub_pcb) {
    mm_zmtp_send_ready(tpcb, MM_ZMQ_SOCKET_PUB);
  } else if (tpcb == agent->sub_pcb) {
    mm_zmtp_send_ready(tpcb, MM_ZMQ_SOCKET_SUB);
    mm_zmtp_send_subscribe(tpcb, agent->config.sub_topic);
  }
  return ERR_OK;
}
#endif

bool mm_zmtp_connect_socket(micromads_agent_t *agent, const char *ip, uint16_t port, mm_zmq_socket_type_t type, void **pcb_ptr) {
  #ifdef USE_W5500
    uint8_t socket_num = (uint8_t)type;
    if (socket(socket_num, Sn_MR_TCP, 0, 0) != socket_num) { *pcb_ptr = NULL; return false; }
    uint8_t target_ip[4];
    unsigned int ip1, ip2, ip3, ip4;
    if (sscanf(ip, "%u.%u.%u.%u", &ip1, &ip2, &ip3, &ip4) == 4) {
        target_ip[0] = (uint8_t)ip1; target_ip[1] = (uint8_t)ip2;
        target_ip[2] = (uint8_t)ip3; target_ip[3] = (uint8_t)ip4;
    } else { *pcb_ptr = NULL; return false; }
    if (connect(socket_num, target_ip, port) != SOCK_OK) { *pcb_ptr = NULL; return false; }
    *pcb_ptr = (void *)(uintptr_t)socket_num; 
    mm_zmtp_send_greeting(*pcb_ptr);
    mm_zmtp_send_ready(*pcb_ptr, type);
    return true;

  #elif defined(USE_ESP32)
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) { *pcb_ptr = NULL; return false; }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &dest_addr.sin_addr);

    int res = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (res < 0 && errno == EINPROGRESS) {
        fd_set fdset; FD_ZERO(&fdset); FD_SET(sock, &fdset);
        struct timeval tv; tv.tv_sec = 2; tv.tv_usec = 0;
        if (select(sock + 1, NULL, &fdset, NULL, &tv) <= 0) {
            close(sock); *pcb_ptr = NULL; return false;
        }
        int so_error; socklen_t s_len = sizeof(so_error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &s_len);
        if (so_error != 0) { close(sock); *pcb_ptr = NULL; return false; }
    } else if (res < 0 && res != EINPROGRESS) {
        close(sock); *pcb_ptr = NULL; return false;
    }

    fcntl(sock, F_SETFL, flags);
    struct timeval tv_rw; tv_rw.tv_sec = 2; tv_rw.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv_rw, sizeof(tv_rw));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv_rw, sizeof(tv_rw));
    int opt_val = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt_val, sizeof(opt_val));
    
    *pcb_ptr = (void *)(uintptr_t)sock; 
    
    if (!mm_zmtp_send_greeting(*pcb_ptr) || !mm_zmtp_send_ready(*pcb_ptr, type)) {
        close(sock); *pcb_ptr = NULL; return false;
    }

    uint8_t server_greeting[64];
    int total_read = 0;
    while (total_read < 64) {
        int r = recv(sock, server_greeting + total_read, 64 - total_read, 0);
        if (r <= 0) { close(sock); *pcb_ptr = NULL; return false; }
        total_read += r;
    }

    uint8_t ready_header[2];
    int h_read = 0;
    while (h_read < 2) {
        int r = recv(sock, ready_header + h_read, 2 - h_read, 0);
        if (r <= 0) { close(sock); *pcb_ptr = NULL; return false; }
        h_read += r;
    }
    
    uint64_t ready_len = 0;
    if (ready_header[0] & 0x02) { 
        uint8_t long_len[7];
        int l_read = 0;
        while (l_read < 7) {
            int r = recv(sock, long_len + l_read, 7 - l_read, 0);
            if (r <= 0) { close(sock); *pcb_ptr = NULL; return false; }
            l_read += r;
        }
        ready_len = ready_header[1];
        for (int i=0; i<7; i++) ready_len = (ready_len << 8) | long_len[i];
    } else {
        ready_len = ready_header[1];
    }

    if (ready_len > 0) {
        uint8_t trash_buf[256];
        uint64_t r_len = 0;
        while (r_len < ready_len) {
            int to_read = (ready_len - r_len > sizeof(trash_buf)) ? sizeof(trash_buf) : (ready_len - r_len);
            int r = recv(sock, trash_buf, to_read, 0);
            if (r <= 0) break;
            r_len += r;
        }
    }
    return true;
  #else
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) return false;
    *pcb_ptr = pcb; 
    tcp_arg(pcb, agent);
    tcp_recv(pcb, mm_tcp_recv_callback);
    tcp_err(pcb, mm_tcp_error_callback);
    ip_addr_t remote_addr;
    if (!ipaddr_aton(ip, &remote_addr)) { tcp_abort(pcb); *pcb_ptr = NULL; return false; }
    err_t err = tcp_connect(pcb, &remote_addr, port, mm_tcp_connect_callback);
    if (err != ERR_OK) { tcp_abort(pcb); *pcb_ptr = NULL; return false; }
    return true;
  #endif
}

bool mm_zmtp_send_settings_request(micromads_agent_t *agent) {
  if (agent -> req_pcb == NULL) return false;
  void *pcb = agent -> req_pcb;
  if (!send_zmtp_frame(pcb, "", 0, true)) return false;
  if (!send_zmtp_frame(pcb, MM_LIB_VERSION, strlen(MM_LIB_VERSION), true)) return false;
  if (!send_zmtp_frame(pcb, "settings", 8, true)) return false;
  if (!send_zmtp_frame(pcb, agent->name, strlen(agent->name), false)) return false;
#if !defined(USE_W5500) && !defined(USE_ESP32)
  tcp_output((struct tcp_pcb *)pcb);
#endif
  return true;
}

bool mm_zmtp_send_timecode_request(micromads_agent_t *agent) {
  if (agent -> req_pcb == NULL) return false;
  void *pcb = agent -> req_pcb;
  if (!send_zmtp_frame(pcb, "", 0, true)) return false;
  if (!send_zmtp_frame(pcb, MM_LIB_VERSION, strlen(MM_LIB_VERSION), true)) return false;
  if (!send_zmtp_frame(pcb, "timecode", 8, false)) return false;
#if !defined(USE_W5500) && !defined(USE_ESP32)
  tcp_output((struct tcp_pcb *)pcb);
#endif
  return true;
}

// INVOLUCRO SNAPPY LETTERALE (Zero CPU, Bypass MADS Feedback)
static int encode_snappy_literal(const char* input, size_t input_len, uint8_t* output) {
    int out_idx = 0;
    size_t len = input_len;

    if (len == 0) { output[out_idx++] = 0; return out_idx; }
    while (len > 0) {
        uint8_t b = len & 0x7F;
        len >>= 7;
        if (len > 0) b |= 0x80;
        output[out_idx++] = b;
    }

    uint32_t lit_len = input_len - 1;
    if (lit_len < 60) {
        output[out_idx++] = (uint8_t)((lit_len << 2) | 0x00);
    } else if (lit_len < 256) {
        output[out_idx++] = (uint8_t)((60 << 2) | 0x00); 
        output[out_idx++] = (uint8_t)(lit_len & 0xFF);
    } else {
        output[out_idx++] = (uint8_t)((61 << 2) | 0x00); 
        output[out_idx++] = (uint8_t)(lit_len & 0xFF);
        output[out_idx++] = (uint8_t)((lit_len >> 8) & 0xFF);
    }

    memcpy(&output[out_idx], input, input_len);
    out_idx += input_len;
    return out_idx;
}

bool mm_zmtp_publish_legacy(micromads_agent_t *agent, const char *topic, const char *json_payload) {
  if (agent -> pub_pcb == NULL) return false;
  void *pcb = agent -> pub_pcb;

  uint8_t tlen = strlen(topic);
  size_t plen = strlen(json_payload); 
  uint8_t snappy_buf[512]; 
  int snappy_len = encode_snappy_literal(json_payload, plen, snappy_buf);

  if (!send_zmtp_frame(pcb, topic, tlen, true)) return false;
  if (!send_zmtp_frame(pcb, (const char*)snappy_buf, snappy_len, false)) return false;
#if !defined(USE_W5500) && !defined(USE_ESP32)
  tcp_output((struct tcp_pcb *)pcb);
#endif
  return true;
}

bool mm_zmtp_send_subscribe(void *pcb_ptr, const char *topic) {
  if (!pcb_ptr) return false;
  uint8_t len = strlen(topic);
  uint8_t sub_frame[MM_MAX_TOPIC_LEN + 1];
  sub_frame[0] = 0x01; 
  memcpy(&sub_frame[1], topic, len);
  if (!send_zmtp_frame(pcb_ptr, (const char *)sub_frame, len + 1, false)) return false;
#if !defined(USE_W5500) && !defined(USE_ESP32)
  tcp_output((struct tcp_pcb *)pcb_ptr);
#endif
  return true;
}

void mm_zmtp_close_pcb(void **pcb_ptr) {
  if (pcb_ptr && *pcb_ptr) {
  #ifdef USE_W5500
    uint8_t socket_num = (uint8_t)((uintptr_t)(*pcb_ptr));
    close(socket_num);
    disconnect(socket_num);
  #elif defined(USE_ESP32)
    close((int)((uintptr_t)(*pcb_ptr)));
  #else
    struct tcp_pcb *pcb = (struct tcp_pcb *)*pcb_ptr;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
  #endif
    *pcb_ptr = NULL;
  }
}

void mm_zmtp_poll(micromads_agent_t *agent) {
#ifdef USE_W5500
    
#elif defined(USE_ESP32)
  if (agent -> req_pcb != NULL) {
    int req_sn = (int)((uintptr_t)agent -> req_pcb);
    static uint8_t temp_buf[8192];
    static size_t acc_len = 0;
    
    int req_len = recv(req_sn, temp_buf + acc_len, sizeof(temp_buf) - acc_len, MSG_DONTWAIT);
    if (req_len > 0) acc_len += req_len;
    
    if (acc_len > 1) {
      size_t offset = 0;
      bool message_complete = false;
      
      while (offset < acc_len) {
        uint8_t flags = temp_buf[offset];
        size_t header_len = 0;
        uint64_t payload_len = 0;
        
        if (flags & 0x02) { 
          if (offset + 9 > acc_len) break;
          header_len = 9;
          for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | temp_buf[offset + 1 + i];
          }
        } else { 
          if (offset + 2 > acc_len) break;
          header_len = 2;
          payload_len = temp_buf[offset + 1];
        }
        
        size_t total_frame_len = header_len + (size_t)payload_len;
        if (offset + total_frame_len > acc_len) break;
        
        if (!(flags & 0x01)) { 
          size_t copy_len = (size_t)payload_len;
          if (copy_len > (MM_MAX_PAYLOAD_LEN - agent->rx_index - 1)) {
            copy_len = MM_MAX_PAYLOAD_LEN - agent->rx_index - 1;
          }
          
          memcpy(agent->rx_buffer + agent->rx_index, &temp_buf[offset + header_len], copy_len);
          agent->rx_index += copy_len;
          agent->rx_buffer[agent->rx_index] = '\0';
          
          message_complete = true;
          break;
        }
        offset += total_frame_len;
      }
      if (message_complete) acc_len = 0; 
    }
  }
  
  if (agent->sub_pcb != NULL) {
    int sub_sn = (int)((uintptr_t)agent->sub_pcb);
    static uint8_t data[8192]; 
    
    int sub_len = recv(sub_sn, data, sizeof(data), MSG_DONTWAIT);
    if (sub_len > 4) {
        uint8_t flags1 = data[0]; uint8_t len1 = data[1];
        if (flags1 == 0x01 && (2 + len1 + 2) < sub_len) {
            char *rx_topic = (char *)&data[2];
            uint8_t flags2 = data[2 + len1];
            uint8_t len2 = data[2 + len1 + 1];
            if (flags2 == 0x00 && (2 + len1 + 2 + len2) <= sub_len) {
                char *rx_payload = (char *)&data[2 + len1 + 2];
                data[2 + len1] = '\0';
                if ((2 + len1 + 2 + len2) < sub_len) data[2 + len1 + 2 + len2] = '\0';
                if (agent->on_command_received != NULL) {
                    agent->on_command_received(rx_topic, rx_payload);
                }
            }
        }
    }
  }
#endif
}