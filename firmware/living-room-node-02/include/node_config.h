#pragma once

#include <stdint.h>

namespace NodeConfig {

constexpr char kNodeId[] = "living-room-node-02";

constexpr char kWifiSsid[] = "Ludwig van Beethoven";
constexpr char kWifiPassword[] = "Anhtrung123";

constexpr char kMqttHost[] = "192.168.1.253";
constexpr uint16_t kMqttPort = 1883;
constexpr char kMqttUsername[] = "";
constexpr char kMqttPassword[] = "";

constexpr char kOtaHostname[] = "living-room-node-02";
constexpr char kOtaPassword[] = "";

constexpr char kAvailabilityTopic[] = "smarthome/living-room-node-02/availability";
constexpr char kTelemetryTopic[] = "smarthome/living-room-node-02/telemetry";
constexpr char kCommandTopic[] = "smarthome/living-room-node-02/command";
constexpr char kStateTopic[] = "smarthome/living-room-node-02/state";

// Edit these pins to match your wiring.
constexpr uint8_t kRelay1Pin = 16;
constexpr uint8_t kRelay2Pin = 14;
constexpr uint8_t kTouch1Pin = 12;
constexpr uint8_t kTouch2Pin = 13;
constexpr uint8_t kLed1Pin = 4;
constexpr uint8_t kLed2Pin = 5;

// Change these if your relay or touch boards use opposite logic.
constexpr bool kRelay1ActiveHigh = true;
constexpr bool kRelay2ActiveHigh = true;
constexpr bool kTouchActiveHigh = true;
constexpr bool kLedActiveHigh = true;

constexpr long kTimezoneOffsetSeconds = 7 * 3600;
constexpr uint8_t kLedBreathStartHour = 22;
constexpr uint8_t kLedBreathEndHour = 6;
constexpr uint16_t kLedBreathPeriodMs = 4200;

constexpr uint32_t kTelemetryIntervalMs = 5000;
constexpr uint32_t kTouchDebounceMs = 80;

}  // namespace
