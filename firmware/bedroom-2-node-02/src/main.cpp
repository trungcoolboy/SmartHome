#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "node_config.h"

namespace {

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

constexpr char kRemoteNode01CommandTopic[] = "smarthome/bedroom-2-node-01/command";
constexpr char kRemoteNode01StateTopic[] = "smarthome/bedroom-2-node-01/state";
constexpr char kRemoteNode01TelemetryTopic[] = "smarthome/bedroom-2-node-01/telemetry";

bool touch_down = false;
bool last_touch_raw = false;
bool remote_relay_on = false;
unsigned long last_touch_change_ms = 0;
unsigned long last_telemetry_ms = 0;
unsigned long last_status_log_ms = 0;
unsigned long last_wifi_begin_ms = 0;
unsigned long last_mqtt_attempt_ms = 0;
unsigned long last_local_action_ms = 0;
unsigned long mqtt_retry_backoff_ms = 2000;
bool wifi_begin_called = false;
bool telemetry_dirty = false;
bool state_dirty = false;
const char* pending_event = "state_sync";
const char* pending_detail = nullptr;

constexpr unsigned long kLocalControlGuardMs = 1500;
constexpr unsigned long kMqttRetryBackoffMinMs = 2000;
constexpr unsigned long kMqttRetryBackoffMaxMs = 30000;

bool read_touch_active() {
  return digitalRead(NodeConfig::kTouchPin) == (NodeConfig::kTouchActiveHigh ? HIGH : LOW);
}

void write_led(bool active) {
  analogWrite(NodeConfig::kLedPin, NodeConfig::kLedActiveHigh ? (active ? 255 : 0) : (active ? 0 : 255));
}

void update_led() {
  if (!mqtt_client.connected()) {
    write_led(((millis() / 180) % 2) != 0);
    return;
  }
  if (read_touch_active()) {
    write_led(true);
    return;
  }
  write_led(false);
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
  doc["touch"] = touch_down;
  doc["remoteRelay"] = remote_relay_on;
  if (detail && detail[0] != '\0') {
    doc["detail"] = detail;
  }

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
  doc["touch"] = touch_down;
  doc["remoteRelay"] = remote_relay_on;

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

  if (state_dirty && publish_state_now(pending_event, pending_detail)) {
    state_dirty = false;
  }

  if (telemetry_dirty && publish_telemetry_now()) {
    telemetry_dirty = false;
  }
}

void mark_local_action() {
  last_local_action_ms = millis();
}

void set_remote_relay_state(bool value, const char* event, const char* detail = nullptr) {
  if (remote_relay_on == value) {
    update_led();
    return;
  }
  remote_relay_on = value;
  update_led();
  queue_state(event, detail);
  telemetry_dirty = true;
}

bool publish_remote_toggle() {
  if (!mqtt_client.connected()) {
    return false;
  }
  JsonDocument doc;
  doc["action"] = "toggle_relay";
  char payload[96];
  const size_t len = serializeJson(doc, payload, sizeof(payload));
  return mqtt_client.publish(kRemoteNode01CommandTopic, reinterpret_cast<const uint8_t*>(payload), len, false);
}

void handle_touch() {
  const bool raw = read_touch_active();
  const unsigned long now_ms = millis();

  if (raw != last_touch_raw) {
    last_touch_raw = raw;
    last_touch_change_ms = now_ms;
  }

  if ((now_ms - last_touch_change_ms) < NodeConfig::kTouchDebounceMs || raw == touch_down) {
    return;
  }

  touch_down = raw;
  update_led();
  mark_local_action();

  if (touch_down) {
    if (publish_remote_toggle()) {
      queue_state("remote_toggle_sent");
    } else {
      queue_state("remote_toggle_failed");
    }
  } else {
    queue_state("touch_release");
  }
  telemetry_dirty = true;
}

void process_remote_payload(const uint8_t* payload, unsigned int length, const char* event_name) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, length) != DeserializationError::Ok) {
    return;
  }
  if (!doc["relay"].is<bool>()) {
    return;
  }
  set_remote_relay_state(doc["relay"].as<bool>(), event_name);
}

void handle_command(char* topic, const uint8_t* payload, unsigned int length) {
  if (strcmp(topic, kRemoteNode01StateTopic) == 0) {
    process_remote_payload(payload, length, "remote_state_sync");
    return;
  }
  if (strcmp(topic, kRemoteNode01TelemetryTopic) == 0) {
    process_remote_payload(payload, length, "remote_telemetry_sync");
    return;
  }
  if (strcmp(topic, NodeConfig::kCommandTopic) != 0) {
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload, length) != DeserializationError::Ok) {
    queue_state("bad_json");
    return;
  }

  const char* action = doc["action"] | "";
  if (strcmp(action, "ping") == 0) {
    queue_state("pong");
    telemetry_dirty = true;
    return;
  }
  if (strcmp(action, "toggle_remote") == 0) {
    mark_local_action();
    if (publish_remote_toggle()) {
      queue_state("remote_toggle_sent", "command");
    } else {
      queue_state("remote_toggle_failed", "command");
    }
    telemetry_dirty = true;
    return;
  }
  if (strcmp(action, "sync_remote") == 0) {
    queue_state("sync_requested");
    telemetry_dirty = true;
    return;
  }

  queue_state("unsupported_command", action);
}

void mqtt_callback(char* topic, uint8_t* payload, unsigned int length) {
  handle_command(topic, payload, length);
}

void ensure_wifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  const unsigned long now_ms = millis();
  if (!wifi_begin_called || (now_ms - last_wifi_begin_ms) > 10000) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(NodeConfig::kWifiSsid, NodeConfig::kWifiPassword);
    wifi_begin_called = true;
    last_wifi_begin_ms = now_ms;
  }
}

void ensure_mqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt_client.connected()) {
    return;
  }

  const unsigned long now_ms = millis();
  if ((now_ms - last_local_action_ms) < kLocalControlGuardMs) {
    return;
  }
  if ((now_ms - last_mqtt_attempt_ms) < mqtt_retry_backoff_ms) {
    return;
  }

  last_mqtt_attempt_ms = now_ms;
  const bool connected = mqtt_client.connect(NodeConfig::kNodeId, NodeConfig::kMqttUsername, NodeConfig::kMqttPassword,
                                             NodeConfig::kAvailabilityTopic, 0, true, "offline");
  if (!connected) {
    mqtt_retry_backoff_ms = mqtt_retry_backoff_ms < kMqttRetryBackoffMaxMs
                                ? std::min(kMqttRetryBackoffMaxMs, mqtt_retry_backoff_ms * 2)
                                : kMqttRetryBackoffMaxMs;
    return;
  }

  mqtt_retry_backoff_ms = kMqttRetryBackoffMinMs;
  publish_availability("online");
  mqtt_client.subscribe(NodeConfig::kCommandTopic);
  mqtt_client.subscribe(kRemoteNode01StateTopic);
  mqtt_client.subscribe(kRemoteNode01TelemetryTopic);
  state_dirty = true;
  telemetry_dirty = true;
}

void setup_ota() {
  ArduinoOTA.setHostname(NodeConfig::kOtaHostname);
  ArduinoOTA.setPassword(NodeConfig::kOtaPassword);
  ArduinoOTA.begin();
}

void init_gpio() {
  pinMode(NodeConfig::kTouchPin, INPUT);
  pinMode(NodeConfig::kLedPin, OUTPUT);
  analogWriteRange(255);
  write_led(false);
}

void status_log() {
  Serial.printf("wifi=%s ip=%s rssi=%d mqtt=%s remoteRelay=%s touch=%s\n",
                WiFi.status() == WL_CONNECTED ? "up" : "down",
                WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "0.0.0.0",
                WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0,
                mqtt_client.connected() ? "up" : "down",
                remote_relay_on ? "on" : "off",
                touch_down ? "on" : "off");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  init_gpio();

  mqtt_client.setServer(NodeConfig::kMqttHost, NodeConfig::kMqttPort);
  mqtt_client.setCallback(mqtt_callback);
  mqtt_client.setBufferSize(512);
  mqtt_client.setKeepAlive(15);
  mqtt_client.setSocketTimeout(1);

  ensure_wifi();
  setup_ota();
  telemetry_dirty = true;
  state_dirty = true;
}

void loop() {
  ensure_wifi();
  ArduinoOTA.handle();

  if (WiFi.status() == WL_CONNECTED) {
    ensure_mqtt();
  }

  mqtt_client.loop();
  handle_touch();
  update_led();
  flush_pending_mqtt();

  const unsigned long now_ms = millis();
  if ((now_ms - last_telemetry_ms) >= NodeConfig::kTelemetryIntervalMs) {
    last_telemetry_ms = now_ms;
    telemetry_dirty = true;
  }
  if ((now_ms - last_status_log_ms) >= 5000) {
    last_status_log_ms = now_ms;
    status_log();
  }
}
