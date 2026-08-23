#ifndef MM_TYPES_H
#define MM_TYPES_H

#include <stdint.h>
#include <stdbool.h>

// Fixed-size configuration constants (no dynamic allocation)
#define MM_MAX_NAME_LEN      32
#define MM_MAX_IP_LEN        16
#define MM_MAX_TOPIC_LEN     64
#define MM_MAX_PAYLOAD_LEN   512 // Max JSON frame size

typedef void (*mm_command_cb_t)(const char *topic, const char *payload);

// MADS INI configuration
typedef struct {
  char pub_endpoint_ip[MM_MAX_IP_LEN];
  uint16_t pub_endpoint_port;
  char sub_endpoint_ip[MM_MAX_IP_LEN];
  uint16_t sub_endpoint_port;
  
  char pub_topic[MM_MAX_TOPIC_LEN];
  char sub_topic[MM_MAX_TOPIC_LEN];
} mm_config_t;

// Agent main structure
typedef struct {
    char name[MM_MAX_NAME_LEN];
    char broker_ip[MM_MAX_IP_LEN];
    uint16_t broker_port;
    
    mm_config_t config;
    
    void *req_pcb;
    void *pub_pcb;
    void *sub_pcb;
    
    uint8_t rx_buffer[MM_MAX_PAYLOAD_LEN];
    uint16_t rx_index;

    int current_state;
    uint32_t state_tick;
    mm_command_cb_t on_command_received;
    
} micromads_agent_t;

#endif // MM_TYPES_H