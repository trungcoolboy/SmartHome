#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <time.h>

#include "node_config.h"

namespace {

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

struct ChannelState {
  const char* key;
  uint8_t relay_pin;
  uint8_t touch_pin;
  uint8_t led_pin;
  bool relay_active_high;
  bool relay_on;
  bool last_touch_active;
  unsigned long last_touch_change_ms;
};

ChannelState channels[] = {
  {"relay1", NodeConfig::kRelay1Pin, NodeConfig::kTouch1Pin, NodeConfig::kLed1Pin, NodeConfig::kRelay1ActiveHigh, false, false, 0},
  {"relay2", NodeConfig::kRelay2Pin, NodeConfig::kTouch2Pin, NodeConfig::kLed2Pin, NodeConfig::kRelay2ActiveHigh, false, false, 0},
};

unsigned long last_telemetry_ms = 0;
unsigned long last_status_log_ms = 0;
unsigned long last_wifi_begin_ms = 0;
unsigned long last_mqtt_attempt_ms = 0;
bool wifi_begin_called = false;
bool time_configured = false;

bool as_output_level(bool active, bool active_high) {
  return active_high ? active : !active;
}

bool read_touch_active(uint8_t pin) {
  const int raw = digitalRead(pin);
  return NodeConfig::kTouchActiveHigh ? (raw == HIGH) : (raw == LOW);
}

void write_led_level(uint8_t pin, uint16_t level) {
  analogWrite(pin, NodeConfig::kLedActiveHigh ? level : (255 - level));
}

bool is_led_breath_window() {
  time_t now = time(nullptr);
  if (now < 100000) {
    return false;
  }

  struct tm local_tm {};
  localtime_r(&now, &local_tm);
  const int hour = local_tm.tm_hour;
  if (NodeConfig::kLedBreathStartHour > NodeConfig::kLedBreathEndHour) {
    return hour >= NodeConfig::kLedBreathStartHour || hour < NodeConfig::kLedBreathEndHour;
  }
  return hour >= NodeConfig::kLedBreathStartHour && hour < NodeConfig::kLedBreathEndHour;
}

uint16_t current_breath_level() {
  const unsigned long phase = millis() % NodeConfig::kLedBreathPeriodMs;
  const float half_period = NodeConfig::kLedBreathPeriodMs / 2.0f;
  float ratio = phase <= half_period ? (phase / half_period) : ((NodeConfig::kLedBreathPeriodMs - phase) / half_period);
  ratio = 0.12f + (0.88f * ratio);
  return static_cast<uint16_t>(ratio * 255.0f);
}

void update_leds() {
  const bool breath = is_led_breath_window();
  for (auto& channel : channels) {
    const bool touch_active = read_touch_active(channel.touch_pin);
    if (touch_active) {
      write_led_level(channel.led_pin, 255);
    } else if (breath) {
      write_led_level(channel.led_pin, current_breath_level());
    } else {
      write_led_level(channel.led_pin, 0);
    }
  }
}

void apply_channel_output(ChannelState& channel) {
  digitalWrite(channel.relay_pin, as_output_level(channel.relay_on, channel.relay_active_high) ? HIGH : LOW);
  update_leds();
}

void publish_availability(const char* value) {
  mqtt_client.publish(NodeConfig::kAvailabilityTopic, value, true);
}

void publish_state(const char* event, const char* channel_key = nullptr, const char* detail = nullptr) {
  JsonDocument doc;
  doc["nodeId"] = NodeConfig::kNodeId;
  doc["event"] = event;
  doc["uptimeMs"] = millis();
  doc["wifiRssi"] = WiFi.RSSI();
  if (channel_key && channel_key[0] != '\0') {
    doc["channel"] = channel_key;
  }
  if (detail && detail[0] != '\0') {
    doc["detail"] = detail;
  }

  JsonArray relay_states = doc["relays"].to<JsonArray>();
  for (const auto& channel : channels) {
    JsonObject item = relay_states.add<JsonObject>();
    item["key"] = channel.key;
    item["on"] = channel.relay_on;
  }

  char payload[320];
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

  JsonArray relay_states = doc["relays"].to<JsonArray>();
  for (const auto& channel : channels) {
    JsonObject item = relay_states.add<JsonObject>();
    item["key"] = channel.key;
    item["on"] = channel.relay_on;
    item["touchActive"] = read_touch_active(channel.touch_pin);
  }

  char payload[384];
  const size_t len = serializeJson(doc, payload, sizeof(payload));
  mqtt_client.publish(NodeConfig::kTelemetryTopic, reinterpret_cast<const uint8_t*>(payload), len, false);
}

void setup_ota() {
  ArduinoOTA.setHostname(NodeConfig::kOtaHostname);
  if (NodeConfig::kOtaPassword[0] != '\0') {
    ArduinoOTA.setPassword(NodeConfig::kOtaPassword);
  }

  ArduinoOTA.onStart([]() {
    publish_state("ota_start");
  });

  ArduinoOTA.onEnd([]() {
    publish_state("ota_end");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    char detail[24];
    snprintf(detail, sizeof(detail), "code_%u", static_cast<unsigned>(error));
    publish_state("ota_error", nullptr, detail);
  });

  ArduinoOTA.begin();
}

ChannelState* find_channel(const char* key) {
  for (auto& channel : channels) {
    if (strcmp(channel.key, key) == 0) {
      return &channel;
    }
  }
  return nullptr;
}

void set_channel(ChannelState& channel, bool on, const char* event) {
  channel.relay_on = on;
  apply_channel_output(channel);
  publish_state(event, channel.key, on ? "on" : "off");
}

void toggle_channel(ChannelState& channel, const char* event) {
  set_channel(channel, !channel.relay_on, event);
}

void handle_command(char* topic, byte* payload, unsigned int length) {
  (void)topic;

  JsonDocument doc;
  const auto err = deserializeJson(doc, payload, length);
  if (err) {
    publish_state("bad_json", nullptr, err.c_str());
    return;
  }

  const char* action = doc["action"] | "";
  if (strcmp(action, "ping") == 0) {
    publish_state("pong");
    return;
  }

  if (strcmp(action, "set_relay") == 0) {
    const char* channel_key = doc["channel"] | "";
    ChannelState* channel = find_channel(channel_key);
    if (!channel) {
      publish_state("unknown_channel", channel_key);
      return;
    }
    set_channel(*channel, doc["value"] | false, "relay_updated");
    return;
  }

  if (strcmp(action, "toggle_relay") == 0) {
    const char* channel_key = doc["channel"] | "";
    ChannelState* channel = find_channel(channel_key);
    if (!channel) {
      publish_state("unknown_channel", channel_key);
      return;
    }
    toggle_channel(*channel, "relay_toggled");
    return;
  }

  publish_state("unknown_action", nullptr, action);
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
    publish_state("mqtt_connected");
  }
}

void ensure_time() {
  if (WiFi.status() != WL_CONNECTED || time_configured) {
    return;
  }
  configTime(NodeConfig::kTimezoneOffsetSeconds, 0, "pool.ntp.org", "time.google.com");
  time_configured = true;
}

void poll_touch_inputs() {
  const unsigned long now = millis();

  for (auto& channel : channels) {
    const bool active = read_touch_active(channel.touch_pin);
    if (active != channel.last_touch_active) {
      if ((now - channel.last_touch_change_ms) >= NodeConfig::kTouchDebounceMs) {
        channel.last_touch_change_ms = now;
        channel.last_touch_active = active;
        apply_channel_output(channel);
        if (active) {
          toggle_channel(channel, "touch_toggle");
        }
      }
    }
  }
}

void init_gpio() {
  analogWriteRange(255);
  for (auto& channel : channels) {
    pinMode(channel.relay_pin, OUTPUT);
    pinMode(channel.led_pin, OUTPUT);
    pinMode(channel.touch_pin, INPUT);
    channel.last_touch_active = read_touch_active(channel.touch_pin);
    channel.last_touch_change_ms = millis();
    apply_channel_output(channel);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("Living Room Node 02 boot");

  init_gpio();
  ensure_wifi();
  ensure_time();
  setup_ota();
  ensure_mqtt();
  publish_state("boot_complete");
  publish_telemetry();
  last_telemetry_ms = millis();
  last_status_log_ms = millis();
}

void loop() {
  ensure_wifi();
  ensure_time();
  ensure_mqtt();
  ArduinoOTA.handle();
  mqtt_client.loop();
  poll_touch_inputs();
  update_leds();

  const unsigned long now = millis();
  if (now - last_telemetry_ms >= NodeConfig::kTelemetryIntervalMs) {
    publish_telemetry();
    last_telemetry_ms = now;
  }

  if (now - last_status_log_ms >= 1000) {
    last_status_log_ms = now;
    Serial.printf(
        "wifi=%s ip=%s rssi=%d mqtt=%s relay1=%s relay2=%s\n",
        WiFi.status() == WL_CONNECTED ? "up" : "down",
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI(),
        mqtt_client.connected() ? "up" : "down",
        channels[0].relay_on ? "on" : "off",
        channels[1].relay_on ? "on" : "off");
  }
}
