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

bool relay_on = false;
bool touch_down = false;
bool last_touch_raw = false;
LedMode led_mode = LedMode::Auto;
unsigned long last_telemetry_ms = 0;
unsigned long last_status_log_ms = 0;
unsigned long last_wifi_begin_ms = 0;
unsigned long last_mqtt_attempt_ms = 0;
unsigned long last_touch_change_ms = 0;
unsigned long last_local_action_ms = 0;
unsigned long mqtt_retry_backoff_ms = 2000;
bool wifi_begin_called = false;
bool telemetry_dirty = false;
bool state_dirty = false;
const char* pending_event = "state_sync";
const char* pending_detail = nullptr;
bool time_configured = false;
constexpr unsigned long kLocalControlGuardMs = 1500;
constexpr unsigned long kMqttRetryBackoffMinMs = 2000;
constexpr unsigned long kMqttRetryBackoffMaxMs = 30000;

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

int current_led_level(bool breath_window) {
  const unsigned long now_ms = millis();
  switch (led_mode) {
    case LedMode::On:
      return 255;
    case LedMode::Off:
      return 0;
    case LedMode::Breathe:
    case LedMode::Auto:
    case LedMode::Pulse: {
      if (led_mode == LedMode::Auto && !breath_window) {
        return 0;
      }
      const unsigned long period = led_mode == LedMode::Pulse ? 1800 : NodeConfig::kLedBreathPeriodMs;
      const unsigned long phase = now_ms % period;
      const float half_period = period / 2.0f;
      float ratio = phase <= half_period ? (phase / half_period) : ((period - phase) / half_period);
      ratio = led_mode == LedMode::Pulse ? (0.04f + (0.96f * ratio)) : (0.12f + (0.88f * ratio));
      return static_cast<int>(ratio * 255.0f);
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

bool read_touch_active() {
  return digitalRead(NodeConfig::kTouchPin) == (NodeConfig::kTouchActiveHigh ? HIGH : LOW);
}

void write_led() {
  const bool touch_active = read_touch_active();
  if (!mqtt_client.connected()) {
    const int level = ((millis() / 180) % 2) ? 255 : 0;
    analogWrite(NodeConfig::kLedPin, NodeConfig::kLedActiveHigh ? level : (255 - level));
    return;
  }
  time_t now = time(nullptr);
  bool breath = false;
  if (now >= 100000) {
    struct tm local_tm {};
    localtime_r(&now, &local_tm);
    const int hour = local_tm.tm_hour;
    breath = NodeConfig::kLedBreathStartHour > NodeConfig::kLedBreathEndHour
                 ? (hour >= NodeConfig::kLedBreathStartHour || hour < NodeConfig::kLedBreathEndHour)
                 : (hour >= NodeConfig::kLedBreathStartHour && hour < NodeConfig::kLedBreathEndHour);
  }

  if (touch_active) {
    analogWrite(NodeConfig::kLedPin, NodeConfig::kLedActiveHigh ? 255 : 0);
    return;
  }
  const int level = current_led_level(breath);
  analogWrite(NodeConfig::kLedPin, NodeConfig::kLedActiveHigh ? level : (255 - level));
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
  doc["ledMode"] = led_mode_name(led_mode);

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
  doc["ledMode"] = led_mode_name(led_mode);

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
  last_local_action_ms = millis();
  apply_output();
  telemetry_dirty = true;
  queue_state(event, on ? "on" : "off");
}

void toggle_relay(const char* event) {
  set_relay(!relay_on, event);
}

void set_led_mode(LedMode mode, const char* event) {
  led_mode = mode;
  write_led();
  telemetry_dirty = true;
  queue_state(event, led_mode_name(led_mode));
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

  if (strcmp(action, "set_led_mode") == 0) {
    LedMode next_mode = led_mode;
    const char* mode = doc["mode"] | "";
    if (!parse_led_mode(mode, next_mode)) {
      queue_state("bad_led_mode", mode);
      return;
    }
    set_led_mode(next_mode, "led_mode_updated");
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

void ensure_time() {
  if (WiFi.status() != WL_CONNECTED || time_configured) {
    return;
  }
  configTime(NodeConfig::kTimezoneOffsetSeconds, 0, "pool.ntp.org", "time.google.com");
  time_configured = true;
}

void init_gpio() {
  analogWriteRange(255);
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
  Serial.println("Bathroom 1 Node 02 boot");

  init_gpio();
  mqtt_retry_backoff_ms = kMqttRetryBackoffMinMs;
  last_local_action_ms = millis();
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
  ensure_time();
  ArduinoOTA.handle();
  mqtt_client.loop();
  flush_pending_mqtt();
  write_led();

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
