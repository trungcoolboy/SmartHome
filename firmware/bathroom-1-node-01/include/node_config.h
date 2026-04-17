#pragma once

#include <stdint.h>

namespace NodeConfig {

constexpr char kNodeId[] = "bathroom-1-node-01";

constexpr char kWifiSsid[] = "Ludwig van Beethoven";
constexpr char kWifiPassword[] = "Anhtrung123";

constexpr char kMqttHost[] = "192.168.1.253";
constexpr uint16_t kMqttPort = 1883;
constexpr char kMqttUsername[] = "";
constexpr char kMqttPassword[] = "";

constexpr char kOtaHostname[] = "bathroom-1-node-01";
constexpr char kOtaPassword[] = "";

constexpr char kAvailabilityTopic[] = "smarthome/bathroom-1-node-01/availability";
constexpr char kTelemetryTopic[] = "smarthome/bathroom-1-node-01/telemetry";
constexpr char kCommandTopic[] = "smarthome/bathroom-1-node-01/command";
constexpr char kStateTopic[] = "smarthome/bathroom-1-node-01/state";

constexpr uint8_t kRelay1Pin = 15;
constexpr uint8_t kRelay2Pin = 16;
constexpr uint8_t kTouch1Pin = 4;
constexpr uint8_t kTouch2Pin = 13;
constexpr uint8_t kTouch3Pin = 14;
constexpr uint8_t kLed1Pin = 5;
constexpr uint8_t kLed2Pin = 12;
constexpr uint8_t kLed3Pin = 0;

constexpr bool kRelay1ActiveHigh = true;
constexpr bool kRelay2ActiveHigh = false;
constexpr bool kTouchActiveHigh = true;
constexpr bool kLedActiveHigh = true;

constexpr long kTimezoneOffsetSeconds = 7 * 3600;
constexpr uint8_t kLedBreathStartHour = 22;
constexpr uint8_t kLedBreathEndHour = 6;
constexpr uint16_t kLedBreathPeriodMs = 4200;

constexpr uint32_t kTelemetryIntervalMs = 5000;
constexpr uint32_t kTouchDebounceMs = 80;

}  // namespace NodeConfig
