#include "mm_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static bool extract_toml_string(const char *toml, const char *key, char *out_buf, size_t max_len) {
  const char *key_ptr = strstr(toml, key);
  if (!key_ptr) return false;
  const char *equal_sign = strchr(key_ptr, '=');
  if (!equal_sign) return false;
  const char *quote1 = strchr(equal_sign, '"');
  if (!quote1) return false;
  const char *quote2 = strchr(quote1 + 1, '"');
  if (!quote2) return false;

  size_t len = quote2 - (quote1 + 1);
  if (len >= max_len) len = max_len - 1;
  strncpy(out_buf, quote1 + 1, len);
  out_buf[len] = '\0';
  return true;
}

static void parse_toml_port(const char *toml, const char *key, uint16_t *port) {
  char endpoint[64];
  if (extract_toml_string(toml, key, endpoint, sizeof(endpoint))) {
    const char *last_colon = strrchr(endpoint, ':');
    if (last_colon) {
      *port = (uint16_t)atoi(last_colon + 1);
    }
  }
}

bool mm_parse_settings(const char *file_data, mm_config_t *config, const char *broker_ip) {
  config->pub_endpoint_port = 9090; 
  config->sub_endpoint_port = 9091; 
  strncpy(config->pub_topic, "espressniff", MM_MAX_TOPIC_LEN);
  strncpy(config->sub_topic, "", MM_MAX_TOPIC_LEN);
  
  extract_toml_string(file_data, "pub_topic", config->pub_topic, MM_MAX_TOPIC_LEN);
  extract_toml_string(file_data, "sub_topic", config->sub_topic, MM_MAX_TOPIC_LEN);
  
  parse_toml_port(file_data, "frontend_address", &config->pub_endpoint_port);
  parse_toml_port(file_data, "backend_address", &config->sub_endpoint_port);

  strncpy(config->pub_endpoint_ip, broker_ip, MM_MAX_IP_LEN - 1);
  strncpy(config->sub_endpoint_ip, broker_ip, MM_MAX_IP_LEN - 1);

  return true; 
}