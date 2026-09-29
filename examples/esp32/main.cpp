/*
 * micro-MADS on ESP32 (Arduino framework, PlatformIO).
 *
 * platformio.ini:
 *   build_flags = -D USE_ESP32
 *   lib_deps = symlink:///path/to/micro-MADS
 *
 * mads.ini on the broker side, e.g.:
 *   [espressniff]
 *   pub_topic = "sensors"
 *   sub_topic = "command"
 */
#include <Arduino.h>
#include <WiFi.h>

#include <micromads.h>
#include <mm_fsm.h> /* MM_STATE_* */

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASS = "your-password";
static const char *BROKER_IP = "192.168.1.10";

static micromads_agent_t agent;

/* Called for every message on the sub topics, payload is JSON text. */
static void on_message(const char *topic, const char *payload) {
  Serial.printf("[%s] %s\n", topic, payload);
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(100);

  /* name = section in mads.ini, 9092 = broker settings port */
  mm_agent_init(&agent, "espressniff", BROKER_IP, 9092);
  mm_agent_set_callback(&agent, on_message);
}

void loop() {
  static uint32_t last = 0;
  static int last_state = -1;

  mm_agent_spin(&agent); /* never blocks: call it on every loop */

  if (agent.current_state != last_state) {
    last_state = agent.current_state;
    Serial.printf("micro-MADS: %s %s\n", mm_agent_state_name(&agent),
                  mm_agent_last_error(&agent) ? mm_agent_last_error(&agent) : "");
  }

  if (mm_agent_is_ready(&agent) && millis() - last >= 2000) {
    char json[96];
    last = millis();
    snprintf(json, sizeof(json), "{\"mq135\": %d, \"uptime\": %lu}", analogRead(4),
             (unsigned long)(millis() / 1000));
    mm_agent_publish(&agent, json);
  }
}
