#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "node_config.h"

namespace {

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

bool relay_on = false;
bool touch_down = false;
bool last_touch_raw = false;
unsigned long last_telemetry_ms = 0;
unsigned long last_status_log_ms = 0;
unsigned long last_wifi_begin_ms = 0;
unsigned long last_mqtt_attempt_ms = 0;
unsigned long last_touch_change_ms = 0;
bool wifi_begin_called = false;
bool telemetry_dirty = false;
bool state_dirty = false;
const char* pending_event = "state_sync";
const char* pending_detail = nullptr;

bool as_output_level(bool active, bool active_high) {
  return active_high ? active : !active;
}

bool read_touch_active() {
  return digitalRead(NodeConfig::kTouchPin) == (NodeConfig::kTouchActiveHigh ? HIGH : LOW);
}

void write_led() {
  digitalWrite(NodeConfig::kLedPin, as_output_level(relay_on, NodeConfig::kLedActiveHigh) ? HIGH : LOW);
}

void apply_output() {
  digitalWrite(NodeConfig::kRelayPin, as_output_level(relay_on, NodeConfig::kRelayActiveHigh) ? HIGH : LOW);
  write_led();
}

bool publish_availability(const char* value) {
  if (!mqtt_client.connected()) {
    return false;
  }
  return mqtt_client.publish(NodeConfig::kAvailabilityTopic, value, true);
}

bool publish_state_now(const char* event, const char* detail = nullptr) {
  if (!mqtt_client.connected()) {
    return false;
  }

  JsonDocument doc;
  doc["nodeId"] = NodeConfig::kNodeId;
  doc["event"] = event;
  doc["uptimeMs"] = millis();
  doc["wifiRssi"] = WiFi.RSSI();
  if (detail && detail[0] != '\0') {
    doc["detail"] = detail;
  }
  doc["relay"] = relay_on;
  doc["touch"] = touch_down;

  char payload[256];
  const size_t len = serializeJson(doc, payload, sizeof(payload));
  return mqtt_client.publish(NodeConfig::kStateTopic, reinterpret_cast<const uint8_t*>(payload), len, false);
}

bool publish_telemetry_now() {
  if (!mqtt_client.connected()) {
    return false;
  }

  JsonDocument doc;
  doc["nodeId"] = NodeConfig::kNodeId;
  doc["uptimeMs"] = millis();
  doc["wifiRssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["relay"] = relay_on;
  doc["touch"] = touch_down;

  char payload[256];
  const size_t len = serializeJson(doc, payload, sizeof(payload));
  return mqtt_client.publish(NodeConfig::kTelemetryTopic, reinterpret_cast<const uint8_t*>(payload), len, false);
}

void queue_state(const char* event, const char* detail = nullptr) {
  pending_event = event;
  pending_detail = detail;
  state_dirty = true;
}

void flush_pending_mqtt() {
  if (!mqtt_client.connected()) {
    return;
  }

  if (state_dirty) {
    if (publish_state_now(pending_event, pending_detail)) {
      state_dirty = false;
    }
  }

  if (telemetry_dirty || (millis() - last_telemetry_ms) >= NodeConfig::kTelemetryIntervalMs) {
    if (publish_telemetry_now()) {
      telemetry_dirty = false;
      last_telemetry_ms = millis();
    }
  }
}

void set_relay(bool on, const char* event) {
  relay_on = on;
  apply_output();
  telemetry_dirty = true;
  queue_state(event, on ? "on" : "off");
}

void toggle_relay(const char* event) {
  set_relay(!relay_on, event);
}

void handle_command(char* topic, byte* payload, unsigned int length) {
  (void)topic;

  JsonDocument doc;
  const auto err = deserializeJson(doc, payload, length);
  if (err) {
    queue_state("bad_json", err.c_str());
    return;
  }

  const char* action = doc["action"] | "";
  if (strcmp(action, "ping") == 0) {
    queue_state("pong");
    return;
  }

  if (strcmp(action, "set_relay") == 0) {
    set_relay(doc["value"] | false, "relay_updated");
    return;
  }

  if (strcmp(action, "toggle_relay") == 0) {
    toggle_relay("relay_toggled");
    return;
  }

  queue_state("unknown_action", action);
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
  if ((now - last_mqtt_attempt_ms) < 2000) {
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
    publish_availability("online");
    mqtt_client.subscribe(NodeConfig::kCommandTopic);
    queue_state("mqtt_connected");
    telemetry_dirty = true;
  }
}

void setup_ota() {
  ArduinoOTA.setHostname(NodeConfig::kOtaHostname);
  if (NodeConfig::kOtaPassword[0] != '\0') {
    ArduinoOTA.setPassword(NodeConfig::kOtaPassword);
  }

  ArduinoOTA.onStart([]() { queue_state("ota_start"); });
  ArduinoOTA.onEnd([]() { queue_state("ota_end"); });
  ArduinoOTA.onError([](ota_error_t error) {
    char detail[24];
    snprintf(detail, sizeof(detail), "code_%u", static_cast<unsigned>(error));
    queue_state("ota_error", detail);
  });

  ArduinoOTA.begin();
}

void init_gpio() {
  pinMode(NodeConfig::kRelayPin, OUTPUT);
  pinMode(NodeConfig::kLedPin, OUTPUT);
  pinMode(NodeConfig::kTouchPin, INPUT);
  apply_output();
  touch_down = read_touch_active();
  last_touch_raw = touch_down;
  last_touch_change_ms = millis();
}

void poll_touch() {
  const bool active = read_touch_active();
  const unsigned long now = millis();
  if (active != last_touch_raw) {
    last_touch_raw = active;
    last_touch_change_ms = now;
  }

  if (touch_down == last_touch_raw || (now - last_touch_change_ms) < NodeConfig::kTouchDebounceMs) {
    return;
  }

  touch_down = last_touch_raw;
  if (touch_down) {
      toggle_relay("touch_toggle");
  } else {
      telemetry_dirty = true;
      queue_state("touch_release");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("Living Room Node 01 boot");

  init_gpio();
  ensure_wifi();
  setup_ota();
  ensure_mqtt();
  queue_state("boot_complete");
  telemetry_dirty = true;
  flush_pending_mqtt();
  last_status_log_ms = millis();
}

void loop() {
  poll_touch();
  ensure_wifi();
  ensure_mqtt();
  ArduinoOTA.handle();
  mqtt_client.loop();
  flush_pending_mqtt();

  const unsigned long now = millis();
  if (now - last_status_log_ms >= 1000) {
    last_status_log_ms = now;
    Serial.printf(
        "wifi=%s ip=%s rssi=%d mqtt=%s relay=%s touch=%s\n",
        WiFi.status() == WL_CONNECTED ? "up" : "down",
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI(),
        mqtt_client.connected() ? "up" : "down",
        relay_on ? "on" : "off",
        touch_down ? "on" : "off");
  }
}
