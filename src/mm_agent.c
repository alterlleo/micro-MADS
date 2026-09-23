#include "mm_agent.h"
#include "micromads.h"
#include "mm_zmtp.h"
#include "mads_config.h"
#include <string.h>

void mm_agent_init(micromads_agent_t *agent, const char *name, const char *broker_ip, uint16_t broker_port) {
  // micromads_agent_t strcut reset
  memset(agent, 0, sizeof(micromads_agent_t));
  
  strncpy(agent -> name, name, MM_MAX_NAME_LEN - 1);
  strncpy(agent -> broker_ip, broker_ip, MM_MAX_IP_LEN - 1);
  agent -> broker_port = broker_port;
  
  agent -> current_state = (int)STATE_INIT;
}

void mm_agent_set_callback(micromads_agent_t *agent, mm_command_cb_t callback) {
    agent -> on_command_received = callback;
}

void mm_agent_spin(micromads_agent_t *agent) {

  #if defined(USE_W5500) || defined(USE_ESP32)
    mm_zmtp_poll(agent);
  #endif
  
  agent -> current_state = (int)run_state((state_t)agent -> current_state, (state_data_t *)agent);
}

bool mm_agent_publish(micromads_agent_t *agent, const char *json_payload) {
  if ((state_t)agent -> current_state == STATE_READY && agent -> pub_pcb != NULL) {

    #ifdef USE_ESP32
      const char* hostname = "esp32";
    #elif defined(USE_W5500)
      const char* hostname = "w5500";
    #else
      const char* hostname = "stm32";
    #endif

    double current_tc = agent->base_timecode + (MM_GET_TICK() - agent -> base_tick) / 1000.0;

    // well-formatted JSON payload with agent_id, hostname, and timecode
    char formatted_payload[MM_MAX_PAYLOAD_LEN];
    if (json_payload[0] == '{') {
      snprintf(formatted_payload, sizeof(formatted_payload),
        "{\"agent_id\":\"%s\",\"hostname\":\"%s\",\"timecode\":%.3f,%s",
        agent -> name, hostname, current_tc, json_payload + 1);

    } else {
      snprintf(formatted_payload, sizeof(formatted_payload),
        "{\"agent_id\":\"%s\",\"hostname\":\"%s\",\"timecode\":%.3f,\"data\":%s}",
        agent -> name, hostname, current_tc, json_payload);
    }

    #ifndef INI_PARSER
    return mm_zmtp_publish_legacy(agent, PUB_TOPIC, formatted_payload);
    #else
    return mm_zmtp_publish_legacy(agent, agent -> config.pub_topic, formatted_payload);
    #endif
  }
  return false;
}