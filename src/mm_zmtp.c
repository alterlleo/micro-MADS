#include "mm_zmtp.h"
#include <string.h>
#include <stdio.h>

// LwIP headers (STM32)
#include "lwip/tcp.h"
#include "lwip/inet.h"
#include "lwip/pbuf.h"

#define MM_LIB_VERSION "2.1.1"

// LwIP low-level callbacks

// TCP recv: accumulate into agent buffer
static err_t mm_tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    micromads_agent_t *agent = (micromads_agent_t *)arg;

    if (p == NULL) {
        // Broker closed
        tcp_close(tpcb);
        agent->req_pcb = NULL;
        return ERR_OK;
    }

    if (err == ERR_OK) {
        // Copy received data (prevent overflow)
        u16_t copy_len = p->tot_len;
        if (copy_len > (MM_MAX_PAYLOAD_LEN - agent->rx_index - 1)) {
            copy_len = MM_MAX_PAYLOAD_LEN - agent->rx_index - 1; // prevent overflow
        }

        pbuf_copy_partial(p, agent->rx_buffer + agent->rx_index, copy_len, 0);
        agent->rx_index += copy_len;
        agent->rx_buffer[agent->rx_index] = '\0'; // NUL terminator

        // Ack received bytes
        tcp_recved(tpcb, p->tot_len);
    }

    // Free pbuf
    pbuf_free(p);
    return ERR_OK;
}

// Callback di errore TCP
static void mm_tcp_error_callback(void *arg, err_t err) {
    micromads_agent_t *agent = (micromads_agent_t *)arg;
    if (agent) {
        agent->req_pcb = NULL;
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

    agent->req_pcb = pcb;
    agent->rx_index = 0; // clear RX buffer

    // Set context and callbacks
    tcp_arg(pcb, agent);
    tcp_recv(pcb, mm_tcp_recv_callback);
    tcp_err(pcb, mm_tcp_error_callback);

    // Async connect
    ip_addr_t remote_addr;
    if (!ipaddr_aton(ip, &remote_addr)) {
        tcp_abort(pcb);
        agent->req_pcb = NULL;
        return false;
    }

    err_t err = tcp_connect(pcb, &remote_addr, port, mm_tcp_connect_callback);
    if (err != ERR_OK) {
        tcp_abort(pcb);
        agent->req_pcb = NULL;
        return false;
    }

    return true;
}

bool mm_zmtp_send_settings_request(micromads_agent_t *agent) {
    if (agent->req_pcb == NULL) return false;
    struct tcp_pcb *pcb = (struct tcp_pcb *)agent->req_pcb;

    // Format MADS REQ for INI: [version][settings][agent]
    char frame_buffer[128];
    int len = snprintf(frame_buffer, sizeof(frame_buffer), "%s\x08settings%s", MM_LIB_VERSION, agent->name);

    if (len <= 0 || len >= (int)sizeof(frame_buffer)) return false;

    // Write and send
    err_t err = tcp_write(pcb, frame_buffer, (u16_t)len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) return false;
    tcp_output(pcb);
    return true;
}

bool mm_zmtp_send_timecode_request(micromads_agent_t *agent) {
    if (agent->req_pcb == NULL) return false;
    struct tcp_pcb *pcb = (struct tcp_pcb *)agent->req_pcb;

    // Timecode REQ: [version][timecode]
    char frame_buffer[64];
    int len = snprintf(frame_buffer, sizeof(frame_buffer), "v%s\x08timecode", MM_LIB_VERSION);

    if (len <= 0 || len >= (int)sizeof(frame_buffer)) return false;
    err_t err = tcp_write(pcb, frame_buffer, (u16_t)len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) return false;
    tcp_output(pcb);
    return true;
}

bool mm_zmtp_publish_legacy(micromads_agent_t *agent, const char *topic, const char *json_payload) {
    if (agent->pub_pcb == NULL) return false;
    struct tcp_pcb *pcb = (struct tcp_pcb *)agent->pub_pcb;

    // Legacy: [topic][JSON]
    // Send topic then payload
    char header_buf[MM_MAX_TOPIC_LEN + 8];
    int h_len = snprintf(header_buf, sizeof(header_buf), "%s", topic);
    
    if (tcp_write(pcb, header_buf, (u16_t)h_len, TCP_WRITE_FLAG_COPY) != ERR_OK) return false;
    if (tcp_write(pcb, json_payload, (u16_t)strlen(json_payload), TCP_WRITE_FLAG_COPY) != ERR_OK) return false;
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