#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "node_config.h"

namespace {

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

bool wifi_begin_called = false;
unsigned long last_wifi_begin_ms = 0;
unsigned long last_mqtt_attempt_ms = 0;
unsigned long last_telemetry_ms = 0;
unsigned long last_input_sample_ms = 0;
unsigned long last_status_log_ms = 0;
unsigned long mqtt_retry_backoff_ms = 2000;

bool input_scan_enabled = true;
uint8_t stable_levels[NodeConfig::kCandidatePinCount] = {0};
uint8_t sampled_levels[NodeConfig::kCandidatePinCount] = {0};
uint8_t sample_streaks[NodeConfig::kCandidatePinCount] = {0};

int8_t probed_pin_index = -1;
bool probe_level = false;
bool probe_active_high = true;
uint8_t probe_toggles_done = 0;
unsigned long last_probe_toggle_ms = 0;

constexpr unsigned long kMqttRetryBackoffMinMs = 2000;
constexpr unsigned long kMqttRetryBackoffMaxMs = 30000;

void publish_json(const char* topic, JsonDocument& doc, bool retained = false) {
  if (!mqtt_client.connected()) {
    return;
  }
  char payload[512];
  const size_t len = serializeJson(doc, payload, sizeof(payload));
  mqtt_client.publish(topic, reinterpret_cast<const uint8_t*>(payload), len, retained);
}

void publish_event(const char* event, const char* detail = nullptr, int pin = -1, int level = -1) {
  JsonDocument doc;
  doc["nodeId"] = NodeConfig::kNodeId;
  doc["event"] = event;
  doc["uptimeMs"] = millis();
  doc["wifiRssi"] = WiFi.RSSI();
  if (detail && detail[0] != '\0') {
    doc["detail"] = detail;
  }
  if (pin >= 0) {
    doc["pin"] = pin;
  }
  if (level >= 0) {
    doc["level"] = level;
  }
  publish_json(NodeConfig::kStateTopic, doc, false);
}

void publish_telemetry() {
  JsonDocument doc;
  doc["nodeId"] = NodeConfig::kNodeId;
  doc["uptimeMs"] = millis();
  doc["wifiRssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["inputScan"] = input_scan_enabled;
  if (probed_pin_index >= 0) {
    doc["probePin"] = NodeConfig::kCandidatePins[probed_pin_index];
    doc["probeLevel"] = probe_level ? 1 : 0;
  }
  JsonArray levels = doc["inputs"].to<JsonArray>();
  for (size_t i = 0; i < NodeConfig::kCandidatePinCount; i++) {
    JsonObject item = levels.add<JsonObject>();
    item["pin"] = NodeConfig::kCandidatePins[i];
    item["level"] = stable_levels[i];
  }
  publish_json(NodeConfig::kTelemetryTopic, doc, false);
}

void publish_availability(const char* value) {
  if (!mqtt_client.connected()) {
    return;
  }
  mqtt_client.publish(NodeConfig::kAvailabilityTopic, value, true);
}

void release_probe_pin() {
  if (probed_pin_index >= 0) {
    pinMode(NodeConfig::kCandidatePins[probed_pin_index], INPUT);
  }
  probed_pin_index = -1;
  probe_level = false;
  probe_toggles_done = 0;
}

void stop_probe(const char* event = "probe_done") {
  const int pin = probed_pin_index >= 0 ? NodeConfig::kCandidatePins[probed_pin_index] : -1;
  release_probe_pin();
  probe_active_high = true;
  if (pin >= 0) {
    publish_event(event, nullptr, pin, -1);
  }
}

void start_probe_for_pin(uint8_t pin, bool active_high) {
  release_probe_pin();
  for (size_t i = 0; i < NodeConfig::kCandidatePinCount; i++) {
    if (NodeConfig::kCandidatePins[i] == pin) {
      probed_pin_index = static_cast<int8_t>(i);
      break;
    }
  }
  if (probed_pin_index < 0) {
    publish_event("bad_probe_pin", nullptr, pin, -1);
    return;
  }
  pinMode(pin, OUTPUT);
  probe_active_high = active_high;
  digitalWrite(pin, active_high ? LOW : HIGH);
  probe_level = false;
  probe_toggles_done = 0;
  last_probe_toggle_ms = millis();
  publish_event("probe_started", nullptr, pin, 0);
}

void set_output_level(uint8_t pin, bool level, bool active_high) {
  release_probe_pin();
  pinMode(pin, OUTPUT);
  digitalWrite(pin, (active_high ? level : !level) ? HIGH : LOW);
  publish_event("output_level_set", nullptr, pin, level ? 1 : 0);
}

void update_probe() {
  if (probed_pin_index < 0) {
    return;
  }
  const unsigned long now = millis();
  if ((now - last_probe_toggle_ms) < NodeConfig::kProbeToggleIntervalMs) {
    return;
  }
  last_probe_toggle_ms = now;
  probe_level = !probe_level;
  digitalWrite(
      NodeConfig::kCandidatePins[probed_pin_index],
      (probe_active_high ? probe_level : !probe_level) ? HIGH : LOW);
  probe_toggles_done++;
  if (probe_toggles_done >= NodeConfig::kProbeToggleCount) {
    stop_probe();
  }
}

void sample_inputs() {
  if (!input_scan_enabled) {
    return;
  }
  const unsigned long now = millis();
  if ((now - last_input_sample_ms) < NodeConfig::kInputSampleIntervalMs) {
    return;
  }
  last_input_sample_ms = now;

  for (size_t i = 0; i < NodeConfig::kCandidatePinCount; i++) {
    const uint8_t pin = NodeConfig::kCandidatePins[i];
    if (probed_pin_index == static_cast<int8_t>(i)) {
      continue;
    }
    pinMode(pin, INPUT);
    const uint8_t level = digitalRead(pin) == HIGH ? 1U : 0U;
    if (level == sampled_levels[i]) {
      if (sample_streaks[i] < 3U) {
        sample_streaks[i]++;
      }
    } else {
      sampled_levels[i] = level;
      sample_streaks[i] = 1U;
    }

    if (sample_streaks[i] >= 2U && stable_levels[i] != sampled_levels[i]) {
      stable_levels[i] = sampled_levels[i];
      publish_event("input_changed", nullptr, pin, stable_levels[i]);
    }
  }
}

void handle_command(char* topic, byte* payload, unsigned int length) {
  (void)topic;
  JsonDocument doc;
  if (deserializeJson(doc, payload, length)) {
    publish_event("bad_json");
    return;
  }

  const char* action = doc["action"] | "";
  if (strcmp(action, "ping") == 0) {
    publish_event("pong");
    return;
  }
  if (strcmp(action, "scan_inputs") == 0) {
    input_scan_enabled = doc["enable"] | true;
    publish_event(input_scan_enabled ? "input_scan_on" : "input_scan_off");
    return;
  }
  if (strcmp(action, "probe_output") == 0) {
    start_probe_for_pin(static_cast<uint8_t>(doc["pin"] | 255), doc["active_high"] | true);
    return;
  }
  if (strcmp(action, "set_output") == 0) {
    set_output_level(
        static_cast<uint8_t>(doc["pin"] | 255),
        doc["level"] | false,
        doc["active_high"] | true);
    return;
  }
  if (strcmp(action, "release_outputs") == 0) {
    release_probe_pin();
    publish_event("outputs_released");
    return;
  }
  if (strcmp(action, "report_inputs") == 0) {
    publish_telemetry();
    publish_event("inputs_reported");
    return;
  }

  publish_event("unknown_action", action);
}

void ensure_wifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  const unsigned long now = millis();
  if (!wifi_begin_called || (now - last_wifi_begin_ms) >= 10000) {
    WiFi.mode(WIFI_STA);
    WiFi.hostname(NodeConfig::kNodeId);
    WiFi.begin(NodeConfig::kWifiSsid, NodeConfig::kWifiPassword);
    wifi_begin_called = true;
    last_wifi_begin_ms = now;
  }
}

void ensure_mqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt_client.connected()) {
    return;
  }
  const unsigned long now = millis();
  if ((now - last_mqtt_attempt_ms) < mqtt_retry_backoff_ms) {
    return;
  }
  last_mqtt_attempt_ms = now;

  mqtt_client.setServer(NodeConfig::kMqttHost, NodeConfig::kMqttPort);
  mqtt_client.setCallback(handle_command);
  mqtt_client.setBufferSize(512);
  mqtt_client.setKeepAlive(15);
  mqtt_client.setSocketTimeout(1);

  const bool connected =
      strlen(NodeConfig::kMqttUsername) == 0
          ? mqtt_client.connect(NodeConfig::kNodeId, NodeConfig::kAvailabilityTopic, 1, true, "offline")
          : mqtt_client.connect(
                NodeConfig::kNodeId,
                NodeConfig::kMqttUsername,
                NodeConfig::kMqttPassword,
                NodeConfig::kAvailabilityTopic,
                1,
                true,
                "offline");

  if (connected) {
    mqtt_retry_backoff_ms = kMqttRetryBackoffMinMs;
    publish_availability("online");
    mqtt_client.subscribe(NodeConfig::kCommandTopic);
    publish_event("mqtt_connected");
    publish_telemetry();
  } else {
    mqtt_retry_backoff_ms = mqtt_retry_backoff_ms < (kMqttRetryBackoffMaxMs / 2)
                                ? mqtt_retry_backoff_ms * 2
                                : kMqttRetryBackoffMaxMs;
  }
}

void setup_ota() {
  ArduinoOTA.setHostname(NodeConfig::kOtaHostname);
  if (NodeConfig::kOtaPassword[0] != '\0') {
    ArduinoOTA.setPassword(NodeConfig::kOtaPassword);
  }
  ArduinoOTA.onStart([]() { publish_event("ota_start"); });
  ArduinoOTA.onEnd([]() { publish_event("ota_end"); });
  ArduinoOTA.onError([](ota_error_t error) {
    char detail[24];
    snprintf(detail, sizeof(detail), "code_%u", static_cast<unsigned>(error));
    publish_event("ota_error", detail);
  });
  ArduinoOTA.begin();
}

void init_gpio() {
  for (size_t i = 0; i < NodeConfig::kCandidatePinCount; i++) {
    pinMode(NodeConfig::kCandidatePins[i], INPUT);
    const uint8_t level = digitalRead(NodeConfig::kCandidatePins[i]) == HIGH ? 1U : 0U;
    stable_levels[i] = level;
    sampled_levels[i] = level;
    sample_streaks[i] = 0U;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("Bathroom 1 Node 01 Discovery boot");

  init_gpio();
  ensure_wifi();
  setup_ota();
  ensure_mqtt();
  publish_event("boot_complete");
}

void loop() {
  ensure_wifi();
  ensure_mqtt();
  ArduinoOTA.handle();
  mqtt_client.loop();
  sample_inputs();
  update_probe();

  const unsigned long now = millis();
  if ((now - last_telemetry_ms) >= NodeConfig::kTelemetryIntervalMs) {
    last_telemetry_ms = now;
    publish_telemetry();
  }
  if ((now - last_status_log_ms) >= 1000) {
    last_status_log_ms = now;
    Serial.printf("wifi=%s ip=%s mqtt=%s scan=%s probePin=%d probeLevel=%d\n",
                  WiFi.status() == WL_CONNECTED ? "up" : "down",
                  WiFi.localIP().toString().c_str(),
                  mqtt_client.connected() ? "up" : "down",
                  input_scan_enabled ? "on" : "off",
                  probed_pin_index >= 0 ? NodeConfig::kCandidatePins[probed_pin_index] : -1,
                  probe_level ? 1 : 0);
  }
}
