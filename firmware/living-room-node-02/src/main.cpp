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

enum class LedMode : uint8_t {
  Auto,
  On,
  Off,
  Breathe,
  BlinkSlow,
  BlinkFast,
  DoubleBlink,
  Heartbeat,
  Pulse,
  Candle,
};

struct ChannelState {
  const char* key;
  uint8_t relay_pin;
  uint8_t touch_pin;
  uint8_t led_pin;
  bool relay_active_high;
  bool relay_on;
  bool touch_active;
  bool last_touch_raw;
  unsigned long last_touch_raw_change_ms;
  unsigned long last_touch_toggle_ms;
  LedMode led_mode;
};

ChannelState channels[] = {
  {"relay1", NodeConfig::kRelay1Pin, NodeConfig::kTouch2Pin, NodeConfig::kLed2Pin, NodeConfig::kRelay1ActiveHigh, false, false, false, 0, 0, LedMode::Auto},
  {"relay2", NodeConfig::kRelay2Pin, NodeConfig::kTouch1Pin, NodeConfig::kLed1Pin, NodeConfig::kRelay2ActiveHigh, false, false, false, 0, 0, LedMode::Auto},
};

unsigned long last_telemetry_ms = 0;
unsigned long last_status_log_ms = 0;
unsigned long last_wifi_begin_ms = 0;
unsigned long last_mqtt_attempt_ms = 0;
unsigned long last_local_action_ms = 0;
unsigned long mqtt_retry_backoff_ms = 2000;
bool wifi_begin_called = false;
bool time_configured = false;
bool telemetry_dirty = false;
bool state_dirty = false;
const char* pending_event = "state_sync";
const char* pending_channel_key = nullptr;
const char* pending_detail = nullptr;
constexpr unsigned long kLocalControlGuardMs = 1500;
constexpr unsigned long kMqttRetryBackoffMinMs = 2000;
constexpr unsigned long kMqttRetryBackoffMaxMs = 30000;
constexpr unsigned long kTouchRetriggerGuardMs = 1500;

bool as_output_level(bool active, bool active_high) {
  return active_high ? active : !active;
}

const char* led_mode_name(LedMode mode) {
  switch (mode) {
    case LedMode::On:
      return "on";
    case LedMode::Off:
      return "off";
    case LedMode::Breathe:
      return "breathe";
    case LedMode::BlinkSlow:
      return "blink_slow";
    case LedMode::BlinkFast:
      return "blink_fast";
    case LedMode::DoubleBlink:
      return "double_blink";
    case LedMode::Heartbeat:
      return "heartbeat";
    case LedMode::Pulse:
      return "pulse";
    case LedMode::Candle:
      return "candle";
    case LedMode::Auto:
    default:
      return "auto";
  }
}

bool parse_led_mode(const char* value, LedMode& mode) {
  if (strcmp(value, "auto") == 0) {
    mode = LedMode::Auto;
    return true;
  }
  if (strcmp(value, "on") == 0) {
    mode = LedMode::On;
    return true;
  }
  if (strcmp(value, "off") == 0) {
    mode = LedMode::Off;
    return true;
  }
  if (strcmp(value, "breathe") == 0) {
    mode = LedMode::Breathe;
    return true;
  }
  if (strcmp(value, "blink_slow") == 0) {
    mode = LedMode::BlinkSlow;
    return true;
  }
  if (strcmp(value, "blink_fast") == 0) {
    mode = LedMode::BlinkFast;
    return true;
  }
  if (strcmp(value, "double_blink") == 0) {
    mode = LedMode::DoubleBlink;
    return true;
  }
  if (strcmp(value, "heartbeat") == 0) {
    mode = LedMode::Heartbeat;
    return true;
  }
  if (strcmp(value, "pulse") == 0) {
    mode = LedMode::Pulse;
    return true;
  }
  if (strcmp(value, "candle") == 0) {
    mode = LedMode::Candle;
    return true;
  }
  return false;
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

uint16_t current_led_level(LedMode mode, bool breath_window) {
  const unsigned long now_ms = millis();
  switch (mode) {
    case LedMode::On:
      return 255;
    case LedMode::Off:
      return 0;
    case LedMode::Breathe:
    case LedMode::Auto:
    case LedMode::Pulse: {
      if (mode == LedMode::Auto && !breath_window) {
        return 0;
      }
      const unsigned long period = mode == LedMode::Pulse ? 1800 : NodeConfig::kLedBreathPeriodMs;
      const unsigned long phase = now_ms % period;
      const float half_period = period / 2.0f;
      float ratio = phase <= half_period ? (phase / half_period) : ((period - phase) / half_period);
      ratio = mode == LedMode::Pulse ? (0.04f + (0.96f * ratio)) : (0.12f + (0.88f * ratio));
      return static_cast<uint16_t>(ratio * 255.0f);
    }
    case LedMode::BlinkSlow:
      return ((now_ms / 700) % 2) ? 255 : 0;
    case LedMode::BlinkFast:
      return ((now_ms / 180) % 2) ? 255 : 0;
    case LedMode::DoubleBlink: {
      const unsigned long phase = now_ms % 1400;
      return (phase < 120 || (phase >= 240 && phase < 360)) ? 255 : 0;
    }
    case LedMode::Heartbeat: {
      const unsigned long phase = now_ms % 1500;
      return (phase < 90 || (phase >= 140 && phase < 230)) ? 255 : 0;
    }
    case LedMode::Candle: {
      const unsigned long phase = now_ms % 997;
      const int base = 150 + static_cast<int>((phase * 73UL) % 70);
      const int dip = (phase % 173 < 20) ? 60 : 0;
      const int level = base - dip;
      return level < 0 ? 0 : (level > 255 ? 255 : level);
    }
    default:
      return 0;
  }
}

void update_leds() {
  if (!mqtt_client.connected()) {
    const uint16_t level = current_led_level(LedMode::BlinkFast, false);
    for (auto& channel : channels) {
      write_led_level(channel.led_pin, level);
    }
    return;
  }
  const bool breath = is_led_breath_window();
  for (auto& channel : channels) {
    if (channel.touch_active) {
      write_led_level(channel.led_pin, 255);
    } else {
      write_led_level(channel.led_pin, current_led_level(channel.led_mode, breath));
    }
  }
}

void apply_channel_output(ChannelState& channel) {
  digitalWrite(channel.relay_pin, as_output_level(channel.relay_on, channel.relay_active_high) ? HIGH : LOW);
  update_leds();
}

bool publish_availability(const char* value) {
  if (!mqtt_client.connected()) {
    return false;
  }
  if (!mqtt_client.publish(NodeConfig::kAvailabilityTopic, value, true)) {
    Serial.println("mqtt publish availability failed");
    return false;
  }
  return true;
}

bool publish_state_now(const char* event, const char* channel_key = nullptr, const char* detail = nullptr) {
  if (!mqtt_client.connected()) {
    return false;
  }
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
    item["ledMode"] = led_mode_name(channel.led_mode);
  }

  char payload[320];
  const size_t len = serializeJson(doc, payload, sizeof(payload));
  if (!mqtt_client.publish(NodeConfig::kStateTopic, reinterpret_cast<const uint8_t*>(payload), len, false)) {
    Serial.printf("mqtt publish state failed len=%u\n", static_cast<unsigned>(len));
    return false;
  }
  return true;
}

void queue_state(const char* event, const char* channel_key = nullptr, const char* detail = nullptr) {
  pending_event = event;
  pending_channel_key = channel_key;
  pending_detail = detail;
  state_dirty = true;
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

  JsonArray relay_states = doc["relays"].to<JsonArray>();
  for (const auto& channel : channels) {
    JsonObject item = relay_states.add<JsonObject>();
    item["key"] = channel.key;
    item["on"] = channel.relay_on;
    item["touchActive"] = channel.touch_active;
    item["ledMode"] = led_mode_name(channel.led_mode);
  }

  char payload[384];
  const size_t len = serializeJson(doc, payload, sizeof(payload));
  if (!mqtt_client.publish(NodeConfig::kTelemetryTopic, reinterpret_cast<const uint8_t*>(payload), len, false)) {
    Serial.printf("mqtt publish telemetry failed len=%u\n", static_cast<unsigned>(len));
    return false;
  }
  return true;
}

void setup_ota() {
  ArduinoOTA.setHostname(NodeConfig::kOtaHostname);
  if (NodeConfig::kOtaPassword[0] != '\0') {
    ArduinoOTA.setPassword(NodeConfig::kOtaPassword);
  }

  ArduinoOTA.onStart([]() {
    queue_state("ota_start");
  });

  ArduinoOTA.onEnd([]() {
    queue_state("ota_end");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    char detail[24];
    snprintf(detail, sizeof(detail), "code_%u", static_cast<unsigned>(error));
    queue_state("ota_error", nullptr, detail);
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
  last_local_action_ms = millis();
  apply_channel_output(channel);
  telemetry_dirty = true;
  queue_state(event, channel.key, on ? "on" : "off");
}

void toggle_channel(ChannelState& channel, const char* event) {
  set_channel(channel, !channel.relay_on, event);
}

void set_channel_led_mode(ChannelState& channel, LedMode mode, const char* event) {
  channel.led_mode = mode;
  update_leds();
  telemetry_dirty = true;
  queue_state(event, channel.key, led_mode_name(channel.led_mode));
}

void handle_command(char* topic, byte* payload, unsigned int length) {
  (void)topic;

  JsonDocument doc;
  const auto err = deserializeJson(doc, payload, length);
  if (err) {
    queue_state("bad_json", nullptr, err.c_str());
    return;
  }

  const char* action = doc["action"] | "";
  if (strcmp(action, "ping") == 0) {
    queue_state("pong");
    return;
  }

  if (strcmp(action, "set_relay") == 0) {
    const char* channel_key = doc["channel"] | "";
    ChannelState* channel = find_channel(channel_key);
    if (!channel) {
      queue_state("unknown_channel", channel_key);
      return;
    }
    set_channel(*channel, doc["value"] | false, "relay_updated");
    return;
  }

  if (strcmp(action, "toggle_relay") == 0) {
    const char* channel_key = doc["channel"] | "";
    ChannelState* channel = find_channel(channel_key);
    if (!channel) {
      queue_state("unknown_channel", channel_key);
      return;
    }
    toggle_channel(*channel, "relay_toggled");
    return;
  }

  if (strcmp(action, "set_led_mode") == 0) {
    const char* channel_key = doc["channel"] | "";
    ChannelState* channel = find_channel(channel_key);
    if (!channel) {
      queue_state("unknown_channel", channel_key);
      return;
    }
    LedMode next_mode = channel->led_mode;
    const char* mode = doc["mode"] | "";
    if (!parse_led_mode(mode, next_mode)) {
      queue_state("bad_led_mode", channel_key, mode);
      return;
    }
    set_channel_led_mode(*channel, next_mode, "led_mode_updated");
    return;
  }

  queue_state("unknown_action", nullptr, action);
}

void flush_pending_mqtt() {
  if (!mqtt_client.connected()) {
    return;
  }

  if (state_dirty) {
    if (publish_state_now(pending_event, pending_channel_key, pending_detail)) {
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
  if ((now - last_local_action_ms) < kLocalControlGuardMs) {
    return;
  }
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
    queue_state("mqtt_connected");
    telemetry_dirty = true;
  } else {
    mqtt_retry_backoff_ms = mqtt_retry_backoff_ms < (kMqttRetryBackoffMaxMs / 2)
                                ? mqtt_retry_backoff_ms * 2
                                : kMqttRetryBackoffMaxMs;
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
    const bool raw = read_touch_active(channel.touch_pin);
    if (raw != channel.last_touch_raw) {
      channel.last_touch_raw = raw;
      channel.last_touch_raw_change_ms = now;
    }

    if (raw == channel.touch_active) {
      continue;
    }

    if ((now - channel.last_touch_raw_change_ms) < NodeConfig::kTouchDebounceMs) {
      continue;
    }

    channel.touch_active = raw;
    apply_channel_output(channel);
    if (channel.touch_active && (now - channel.last_touch_toggle_ms) >= kTouchRetriggerGuardMs) {
      channel.last_touch_toggle_ms = now;
      toggle_channel(channel, "touch_toggle");
    }
  }
}

void init_gpio() {
  analogWriteRange(255);
  for (auto& channel : channels) {
    pinMode(channel.relay_pin, OUTPUT);
    pinMode(channel.led_pin, OUTPUT);
    pinMode(channel.touch_pin, INPUT);
    channel.last_touch_raw = read_touch_active(channel.touch_pin);
    channel.touch_active = channel.last_touch_raw;
    channel.last_touch_raw_change_ms = millis();
    channel.last_touch_toggle_ms = 0;
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
  mqtt_retry_backoff_ms = kMqttRetryBackoffMinMs;
  last_local_action_ms = millis();
  ensure_wifi();
  ensure_time();
  setup_ota();
  ensure_mqtt();
  queue_state("boot_complete");
  telemetry_dirty = true;
  flush_pending_mqtt();
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
  flush_pending_mqtt();

  const unsigned long now = millis();
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
