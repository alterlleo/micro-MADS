/*
 * Live test against a real mads broker:
 *   ./test_broker [broker_ip] [settings_port] [agent_name] [seconds]
 * Connects, prints what it learned from mads.ini, publishes a message every
 * 500 ms and prints every message received on its sub topics. Exit code 0 if
 * the agent reached the ready state.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "micromads.h"
#include "mm_fsm.h"

static int received = 0;

static void on_message(const char *topic, const char *payload) {
  received++;
  printf("  received [%s] %s\n", topic, payload);
}

int main(int argc, char **argv) {
  const char *ip = argc > 1 ? argv[1] : "127.0.0.1";
  uint16_t port = (uint16_t)(argc > 2 ? atoi(argv[2]) : 9092);
  const char *name = argc > 3 ? argv[3] : "stm32_board";
  uint32_t seconds = (uint32_t)(argc > 4 ? atoi(argv[4]) : 5);
  static micromads_agent_t agent;
  int last_state = -1, was_ready = 0;
  uint32_t start = mm_millis(), last_pub = 0, i = 0;

  mm_agent_init(&agent, name, ip, port);
  mm_agent_set_callback(&agent, on_message);

  while (mm_millis() - start < seconds * 1000u) {
    struct timespec ts = {0, 1000000};
    mm_agent_spin(&agent);
    if (agent.current_state != last_state) {
      last_state = agent.current_state;
      printf("%5lu ms  %s%s%s\n", (unsigned long)(mm_millis() - start),
             mm_agent_state_name(&agent), mm_agent_last_error(&agent) ? "  error: " : "",
             mm_agent_last_error(&agent) ? mm_agent_last_error(&agent) : "");
      if (mm_agent_is_ready(&agent) && !was_ready) {
        uint8_t t;
        was_ready = 1;
        printf("  ports %u/%u, timecode_fps %u, pub_topic \"%s\", sub_topics:",
               agent.pub_port, agent.sub_port, agent.timecode_fps, mm_agent_pub_topic(&agent));
        for (t = 0; t < mm_agent_sub_topic_count(&agent); t++)
          printf(" \"%s\"", mm_agent_sub_topic(&agent, t));
        printf("\n  broker timecode at start: %s\n", agent.reply_text);
      }
    }
    if (mm_agent_is_ready(&agent) && mm_millis() - last_pub >= 500) {
      char json[400];
      last_pub = mm_millis();
      if (i % 5 == 4) /* > 255 bytes: needs the long ZMTP frame form */
        snprintf(json, sizeof(json), "{\"count\": %lu, \"pad\": \"%0300d\"}",
                 (unsigned long)i++, 0);
      else
        snprintf(json, sizeof(json), "{\"count\": %lu, \"ok\": true}", (unsigned long)i++);
      if (!mm_agent_publish(&agent, json)) printf("  publish failed\n");
    }
    nanosleep(&ts, NULL);
  }
  printf("published %lu, received %lu, dropped %lu\n", (unsigned long)agent.published,
         (unsigned long)agent.received, (unsigned long)agent.dropped);
  return was_ready ? 0 : 1;
}
