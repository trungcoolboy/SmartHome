#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "node_config.h"

namespace {
WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

unsigned long last_telemetry_ms = 0;
unsigned long boot_ms = 0;
bool led_state = false;

void publish_availability(const char* value) {
  mqtt_client.publish(NodeConfig::kAvailabilityTopic, value, true);
}

void publish_state(const char* event, const char* detail = nullptr) {
  JsonDocument doc;
  doc["nodeId"] = NodeConfig::kNodeId;
  doc["event"] = event;
  doc["uptimeMs"] = millis();
  doc["wifiRssi"] = WiFi.RSSI();
  if (detail && detail[0] != '\0') {
    doc["detail"] = detail;
  }

  char payload[192];
  const size_t len = serializeJson(doc, payload, sizeof(payload));
  mqtt_client.publish(NodeConfig::kStateTopic, reinterpret_cast<const uint8_t*>(payload), len, false);
}

void publish_telemetry() {
  JsonDocument doc;
  doc["nodeId"] = NodeConfig::kNodeId;
  doc["uptimeMs"] = millis();
  doc["wifiRssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["ledState"] = led_state;

  char payload[224];
  const size_t len = serializeJson(doc, payload, sizeof(payload));
  mqtt_client.publish(NodeConfig::kTelemetryTopic, reinterpret_cast<const uint8_t*>(payload), len, false);
}

void handle_command(char* topic, byte* payload, unsigned int length) {
  (void)topic;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    publish_state("bad_json", err.c_str());
    return;
  }

  const char* action = doc["action"] | "";
  if (strcmp(action, "ping") == 0) {
    publish_state("pong");
    return;
  }

  if (strcmp(action, "set_led") == 0) {
    led_state = doc["value"] | false;
    digitalWrite(LED_BUILTIN, led_state ? LOW : HIGH);
    publish_state("led_updated", led_state ? "on" : "off");
    return;
  }

  publish_state("unknown_action", action);
}

void ensure_wifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.hostname(NodeConfig::kNodeId);
  WiFi.begin(NodeConfig::kWifiSsid, NodeConfig::kWifiPassword);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void ensure_mqtt() {
  if (mqtt_client.connected()) {
    return;
  }

  mqtt_client.setServer(NodeConfig::kMqttHost, NodeConfig::kMqttPort);
  mqtt_client.setCallback(handle_command);

  while (!mqtt_client.connected()) {
    const bool connected =
        strlen(NodeConfig::kMqttUsername) == 0
            ? mqtt_client.connect(
                  NodeConfig::kNodeId,
                  NodeConfig::kAvailabilityTopic,
                  1,
                  true,
                  "offline")
            : mqtt_client.connect(
                  NodeConfig::kNodeId,
                  NodeConfig::kMqttUsername,
                  NodeConfig::kMqttPassword,
                  NodeConfig::kAvailabilityTopic,
                  1,
                  true,
                  "offline");

    if (!connected) {
      delay(2000);
      continue;
    }

    publish_availability("online");
    mqtt_client.subscribe(NodeConfig::kCommandTopic);
    publish_state("mqtt_connected");
  }
}
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(50);
  boot_ms = millis();

  Serial.println();
  Serial.println("ESP-12S #01 boot");

  ensure_wifi();
  ensure_mqtt();
  publish_state("boot_complete");
  publish_telemetry();
  last_telemetry_ms = millis();
}

void loop() {
  ensure_wifi();
  ensure_mqtt();
  mqtt_client.loop();

  const unsigned long now = millis();
  if (now - last_telemetry_ms >= 5000) {
    publish_telemetry();
    last_telemetry_ms = now;
  }

  if (now - boot_ms >= 1000) {
    boot_ms = now;
    Serial.printf(
        "wifi=%s ip=%s rssi=%d mqtt=%s\n",
        WiFi.status() == WL_CONNECTED ? "up" : "down",
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI(),
        mqtt_client.connected() ? "up" : "down");
  }
}
