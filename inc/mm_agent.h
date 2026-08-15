#ifndef __MM_AGENT_H__
#define __MM_AGENT_H__

#include "mm_types.h"

typedef void (*mm_command_cb_t)(const char *topic, const char *payload);

void mm_agent_init(micromads_agent_t *agent, const char *name, const char *broker_ip, uint16_t broker_port);

void mm_agent_set_callback(micromads_agent_t *agent, mm_command_cb_t callback);

void mm_agent_spin(micromads_agent_t *agent);

bool mm_agent_publish(micromads_agent_t *agent, const char *json_payload);

#endif

