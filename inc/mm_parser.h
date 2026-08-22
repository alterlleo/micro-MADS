#ifndef __MM_PARSER_H__
#define __MM_PARSER_H__

#include "mm_types.h"
#include <stdbool.h>

bool mm_parse_settings(const char *json_data, mm_config_t *config, const char *broker_ip);

#endif