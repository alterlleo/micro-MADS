#include "mm_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static bool extract_toml_string_scoped(const char *toml, const char *section, const char *key, char *out_buf, size_t max_len) {
    char sec_header[64];
    snprintf(sec_header, sizeof(sec_header), "[%s]", section);
    
    const char *curr = strstr(toml, sec_header);
    if (!curr) return false;
    
    curr = strchr(curr, '\n');
    size_t key_len = strlen(key);
    
    while (curr) {
        curr++; 
        while (*curr == ' ' || *curr == '\t') curr++;
        
        if (*curr == '[') break;
        
        if (*curr == '#' || *curr == '\r' || *curr == '\n') {
            curr = strchr(curr, '\n');
            continue;
        }
        
        if (strncmp(curr, key, key_len) == 0) {
            const char *after_key = curr + key_len;
            while (*after_key == ' ' || *after_key == '\t') after_key++;
            
            if (*after_key == '=') {
                const char *val_start = strchr(after_key, '"');
                if (!val_start) return false;
                
                const char *val_end = strchr(val_start + 1, '"');
                if (!val_end) return false;
                
                size_t len = val_end - (val_start + 1);
                
                if (len == 0) return false; 
                
                if (len >= max_len) len = max_len - 1;
                strncpy(out_buf, val_start + 1, len);
                out_buf[len] = '\0';
                return true;
            }
        }
        curr = strchr(curr, '\n');
    }
    return false;
}

static void parse_toml_port(const char *toml, const char *section, const char *key, uint16_t *port) {
  char endpoint[64];
  if (extract_toml_string_scoped(toml, section, key, endpoint, sizeof(endpoint))) {
    const char *last_colon = strrchr(endpoint, ':');
    if (last_colon) {
      *port = (uint16_t)atoi(last_colon + 1);
    }
  }
}

bool mm_parse_settings(const char *file_data, mm_config_t *config, const char *broker_ip, const char *agent_name) {
  if (!file_data || !config) return false;

  config->pub_endpoint_port = 9090; 
  config->sub_endpoint_port = 9091; 
  
  strncpy(config->pub_topic, "sensors", MM_MAX_TOPIC_LEN); 
  strncpy(config->sub_topic, "control", MM_MAX_TOPIC_LEN); 
  
  const char* target_sec = (agent_name && strlen(agent_name) > 0) ? agent_name : "espressniff";

  extract_toml_string_scoped(file_data, target_sec, "pub_topic", config->pub_topic, MM_MAX_TOPIC_LEN);
  extract_toml_string_scoped(file_data, target_sec, "sub_topic", config->sub_topic, MM_MAX_TOPIC_LEN);

  parse_toml_port(file_data, "agents", "frontend_address", &config->pub_endpoint_port);
  parse_toml_port(file_data, "agents", "backend_address", &config->sub_endpoint_port);

  strncpy(config->pub_endpoint_ip, broker_ip, MM_MAX_IP_LEN - 1);
  strncpy(config->sub_endpoint_ip, broker_ip, MM_MAX_IP_LEN - 1);

  return true;
}