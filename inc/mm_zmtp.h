#ifndef __MM_ZMTP_H__
#define __MM_ZMTP_H__

#include <stdint.h>
#include <stdbool.h>
#include "mm_types.h"

bool mm_zmtp_connect_req(micromads_agent_t *agent, const char *ip, uint16_t port);

bool mm_zmtp_send_settings_request(micromads_agent_t *agent);

bool mm_zmtp_send_timecode_request(micromads_agent_t *agent);

bool mm_zmtp_publish_legacy(micromads_agent_t *agent, const char *topic, const char *json_payload);

void mm_zmtp_close_pcb(void **pcb);

#endif