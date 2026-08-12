#include "mm_zmtp.h"
#include <string.h>
#include <stdio.h>

// LwIP headers (STM32)
#include "lwip/tcp.h"
#include "lwip/inet.h"
#include "lwip/pbuf.h"

#define MM_LIB_VERSION "2.1.1"

/*
  _______  __ _____ ____    __  __                          
 |__  /  \/  |_   _|  _ \  |  \/  | __ _  ___ _ __ ___  ___ 
   / /| |\/| | | | | |_) | | |\/| |/ _` |/ __| '__/ _ \/ __|
  / /_| |  | | | | |  __/  | |  | | (_| | (__| | | (_) \__ \
 /____|_|  |_| |_| |_|     |_|  |_|\__,_|\___|_|  \___/|___/
                                                            
*/
static const uint8_t ZMTP_GREETING[64] = {
    0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, // Signature
    0x03, 0x00,                                                 // Version 3.0
    'N', 'U', 'L', 'L', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // Mechanism
    0x00,                                                       // Client mode
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const uint8_t ZMTP_READY_REQ[27] = {
    0x04, 25,                                     // Flag(Command), Size(25)
    0x05, 'R','E','A','D','Y',                    // Command Name
    0x0B, 'S','o','c','k','e','t','-','T','y','p','e', // Metadata Key
    0x00, 0x00, 0x00, 0x03, 'R','E','Q'           // Metadata Value (32-bit len + string)
};

static const uint8_t ZMTP_READY_PUB[27] = {
    0x04, 25,
    0x05, 'R','E','A','D','Y',
    0x0B, 'S','o','c','k','e','t','-','T','y','p','e',
    0x00, 0x00, 0x00, 0x03, 'P','U','B'
};

static const uint8_t ZMTP_READY_SUB[27] = {
    0x04, 25,
    0x05, 'R','E','A','D','Y',
    0x0B, 'S','o','c','k','e','t','-','T','y','p','e',
    0x00, 0x00, 0x00, 0x03, 'S','U','B'
};

// Packet and send a single ZMTP frame over TCP
static bool send_zmtp_frame(struct tcp_pcb *pcb, const char *data, uint8_t len, bool more) {
    uint8_t header[2];
    header[0] = more ? 0x01 : 0x00; // Flag MORE
    header[1] = len;

    if (tcp_write(pcb, header, 2, TCP_WRITE_FLAG_COPY) != ERR_OK) return false;
    if (tcp_write(pcb, data, len, TCP_WRITE_FLAG_COPY) != ERR_OK) return false;
    
    return true;
}


/*
  _   _                 _     _           _        
 | | | | __ _ _ __   __| |___| |__   __ _| | _____ 
 | |_| |/ _` | '_ \ / _` / __| '_ \ / _` | |/ / _ \
 |  _  | (_| | | | | (_| \__ \ | | | (_| |   <  __/
 |_| |_|\__,_|_| |_|\__,_|___/_| |_|\__,_|_|\_\___|
                                                   
*/

bool mm_zmtp_send_greeting(void *pcb_ptr) {
    if (!pcb_ptr) return false;
    struct tcp_pcb *pcb = (struct tcp_pcb *)pcb_ptr;
    
    err_t err = tcp_write(pcb, ZMTP_GREETING, sizeof(ZMTP_GREETING), TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) tcp_output(pcb);
    return (err == ERR_OK);
}

bool mm_zmtp_send_ready(void *pcb_ptr, mm_zmq_socket_type_t socket_type) {
    if (!pcb_ptr) return false;
    struct tcp_pcb *pcb = (struct tcp_pcb *)pcb_ptr;
    
    const uint8_t *ready_frame;
    if (socket_type == MM_ZMQ_SOCKET_REQ) ready_frame = ZMTP_READY_REQ;
    else if (socket_type == MM_ZMQ_SOCKET_PUB) ready_frame = ZMTP_READY_PUB;
    else ready_frame = ZMTP_READY_SUB;

    err_t err = tcp_write(pcb, ready_frame, 27, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) tcp_output(pcb);
    return (err == ERR_OK);
}

/*
     _    ____ ___   __  __           _     
    / \  |  _ \_ _| |  \/  | __ _  __| |___ 
   / _ \ | |_) | |  | |\/| |/ _` |/ _` / __|
  / ___ \|  __/| |  | |  | | (_| | (_| \__ \
 /_/   \_\_|  |___| |_|  |_|\__,_|\__,_|___/
                                            
*/

// TCP recv: accumulate into agent buffer
static err_t mm_tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
  micromads_agent_t *agent = (micromads_agent_t *)arg;

  if (p == NULL) {
    // Broker closed
    tcp_close(tpcb);
    agent -> req_pcb = NULL;
    return ERR_OK;
  }

  if (err == ERR_OK) {
    // Copy received data (prevent overflow)
    u16_t copy_len = p -> tot_len;
    if (copy_len > (MM_MAX_PAYLOAD_LEN - agent -> rx_index - 1)) {
      copy_len = MM_MAX_PAYLOAD_LEN - agent -> rx_index - 1; // prevent overflow
    }

    pbuf_copy_partial(p, agent -> rx_buffer + agent -> rx_index, copy_len, 0);
    agent -> rx_index += copy_len;
    agent -> rx_buffer[agent -> rx_index] = '\0'; // NUL terminator

    // Ack received bytes
    tcp_recved(tpcb, p -> tot_len);
  }

  // Free pbuf
  pbuf_free(p);
  return ERR_OK;
}

// Callback di errore TCP
static void mm_tcp_error_callback(void *arg, err_t err) {
  micromads_agent_t *agent = (micromads_agent_t *)arg;
  if (agent) {
    agent -> req_pcb = NULL;
    // FSM can handle error
  }
}

// Callback di avvenuta connessione TCP
static err_t mm_tcp_connect_callback(void *arg, struct tcp_pcb *tpcb, err_t err) {
  micromads_agent_t *agent = (micromads_agent_t *)arg;
  if (err != ERR_OK) return err;
  // Connected
  return ERR_OK;
}

// Public API

bool mm_zmtp_connect_req(micromads_agent_t *agent, const char *ip, uint16_t port) {

  // Allocate PCB
  struct tcp_pcb *pcb = tcp_new();
  if (pcb == NULL) return false;

  agent -> req_pcb = pcb;
  agent -> rx_index = 0; // clear RX buffer

  // Set context and callbacks
  tcp_arg(pcb, agent);
  tcp_recv(pcb, mm_tcp_recv_callback);
  tcp_err(pcb, mm_tcp_error_callback);

  // Async connect
  ip_addr_t remote_addr;
  if (!ipaddr_aton(ip, &remote_addr)) {
    tcp_abort(pcb);
    agent -> req_pcb = NULL;
    return false;
  }

  err_t err = tcp_connect(pcb, &remote_addr, port, mm_tcp_connect_callback);
  if (err != ERR_OK) {
    tcp_abort(pcb);
    agent -> req_pcb = NULL;
    return false;
  }

  return true;
}

bool mm_zmtp_send_settings_request(micromads_agent_t *agent) {
  if (agent -> req_pcb == NULL) return false;
  struct tcp_pcb *pcb = (struct tcp_pcb *)agent -> req_pcb;

  if (!send_zmtp_frame(pcb, MM_LIB_VERSION, strlen(MM_LIB_VERSION), true)) return false;
  if (!send_zmtp_frame(pcb, "settings", 8, true)) return false;
  if (!send_zmtp_frame(pcb, agent->name, strlen(agent->name), false)) return false;

  tcp_output(pcb);
  return true;
}

bool mm_zmtp_send_timecode_request(micromads_agent_t *agent) {
  if (agent -> req_pcb == NULL) return false;
  struct tcp_pcb *pcb = (struct tcp_pcb *)agent -> req_pcb;

  char ver_buf[16];
  uint8_t vlen = snprintf(ver_buf, sizeof(ver_buf), "v%s", MM_LIB_VERSION);

  // ["v2.1.1", "timecode"]
  if (!send_zmtp_frame(pcb, ver_buf, vlen, true)) return false;
  if (!send_zmtp_frame(pcb, "timecode", 8, false)) return false;

  tcp_output(pcb);
  return true;
}

bool mm_zmtp_publish_legacy(micromads_agent_t *agent, const char *topic, const char *json_payload) {
  if (agent -> pub_pcb == NULL) return false;
  struct tcp_pcb *pcb = (struct tcp_pcb *)agent -> pub_pcb;

  // Legacy frame PUB: [topic, json_payload]
  uint8_t tlen = strlen(topic);
  uint8_t plen = strlen(json_payload); // Nota: per payload > 255 byte servirà gestire i frame long (flag 0x02), ma per ora ci teniamo bassi.

  if (!send_zmtp_frame(pcb, topic, tlen, true)) return false;
  if (!send_zmtp_frame(pcb, json_payload, plen, false)) return false;

  tcp_output(pcb);
  return true;
}

bool mm_zmtp_send_subscribe(void *pcb_ptr, const char *topic) {
  if (!pcb_ptr) return false;
  struct tcp_pcb *pcb = (struct tcp_pcb *)pcb_ptr;

  uint8_t len = strlen(topic);
  uint8_t sub_frame[MM_MAX_TOPIC_LEN + 1];
  
  sub_frame[0] = 0x01; // Prefix for "Subscribe"
  memcpy(&sub_frame[1], topic, len);
  if (!send_zmtp_frame(pcb, (const char *)sub_frame, len + 1, false)) return false;
  
  tcp_output(pcb);
  return true;
}

void mm_zmtp_close_pcb(void **pcb_ptr) {
  if (pcb_ptr && *pcb_ptr) {
    struct tcp_pcb *pcb = (struct tcp_pcb *)*pcb_ptr;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb); // abort if close fails
    *pcb_ptr = NULL;
  }
}