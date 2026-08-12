#ifndef MM_TYPES_H
#define MM_TYPES_H

#include <stdint.h>
#include <stdbool.h>

// Fixed-size configuration constants (no dynamic allocation)
#define MM_MAX_NAME_LEN      32
#define MM_MAX_IP_LEN        16
#define MM_MAX_TOPIC_LEN     64
#define MM_MAX_PAYLOAD_LEN   512 // Max JSON frame size

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
  // Identity
  char name[MM_MAX_NAME_LEN];
  char broker_ip[MM_MAX_IP_LEN];
  uint16_t broker_port;

  // Configuration obtained during handshake
  mm_config_t config;

  // Network abstraction (LwIP)
  // Use void* for tcp_pcb to avoid including LwIP headers here.
  void* req_pcb; // Control socket (broker/handshake)
  void* pub_pcb; // Publish socket (telemetry)
  void* sub_pcb; // Subscription socket (commands)

  // Static buffer for network I/O
  uint8_t rx_buffer[MM_MAX_PAYLOAD_LEN];
  uint16_t rx_index;

  // Timing / timeout ticks
  uint32_t state_tick; 
} micromads_agent_t;

#endif // MM_TYPES_H