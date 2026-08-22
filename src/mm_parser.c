#include "mm_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static bool extract_json_string(const char *json, const char *key, char *out_buf, size_t max_len) {
  char search_key[64];
  snprintf(search_key, sizeof(search_key), "\"%s\"", key);

  const char *key_ptr = strstr(json, search_key);
  if (!key_ptr) return false;

  const char *colon = strchr(key_ptr, ':');
  if (!colon) return false;
  const char *quote1 = strchr(colon, '"');
  if (!quote1) return false;
  const char *quote2 = strchr(quote1 + 1, '"');
  if (!quote2) return false;

  size_t len = quote2 - (quote1 + 1);
  if (len >= max_len) len = max_len - 1;
  strncpy(out_buf, quote1 + 1, len);
  out_buf[len] = '\0';
  return true;
}

static void parse_json_port(const char *json, const char *key, uint16_t *port) {
  char endpoint[64];
  if (extract_json_string(json, key, endpoint, sizeof(endpoint))) {
    const char *last_colon = strrchr(endpoint, ':');
    if (last_colon) {
      *port = (uint16_t)atoi(last_colon + 1);
    }
  }
}

bool mm_parse_settings(const char *json_data, mm_config_t *config, const char *broker_ip) {
    
  extract_json_string(json_data, "pub_topic", config -> pub_topic, MM_MAX_TOPIC_LEN);
  extract_json_string(json_data, "sub_topic", config -> sub_topic, MM_MAX_TOPIC_LEN);
  parse_json_port(json_data, "backend_address", &config -> pub_endpoint_port);
  parse_json_port(json_data, "frontend_address", &config -> sub_endpoint_port);

  strncpy(config -> pub_endpoint_ip, broker_ip, MM_MAX_IP_LEN - 1);
  strncpy(config -> sub_endpoint_ip, broker_ip, MM_MAX_IP_LEN - 1);

  return (config -> pub_endpoint_port > 0 || config -> sub_endpoint_port > 0);
}