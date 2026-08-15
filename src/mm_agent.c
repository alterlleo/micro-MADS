#include "mm_agent.h"
#include "micromads.h"
#include "mm_zmtp.h"
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
  agent -> current_state = (int)run_state((state_t)agent -> current_state, (state_data_t *)agent);
}

bool mm_agent_publish(micromads_agent_t *agent, const char *json_payload) {
  if ((state_t)agent -> current_state == STATE_READY && agent -> pub_pcb != NULL) {
    return mm_zmtp_publish_legacy(agent, agent -> config.pub_topic, json_payload);
  }
  return false;
}