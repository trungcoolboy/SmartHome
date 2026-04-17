#pragma once

#include <stdint.h>

namespace NodeConfig {

constexpr char kNodeId[] = "bedroom-2-node-01";

constexpr char kWifiSsid[] = "Ludwig van Beethoven";
constexpr char kWifiPassword[] = "Anhtrung123";

constexpr char kMqttHost[] = "192.168.1.253";
constexpr uint16_t kMqttPort = 1883;
constexpr char kMqttUsername[] = "";
constexpr char kMqttPassword[] = "";

constexpr char kOtaHostname[] = "bedroom-2-node-01";
constexpr char kOtaPassword[] = "";

constexpr char kAvailabilityTopic[] = "smarthome/bedroom-2-node-01/availability";
constexpr char kTelemetryTopic[] = "smarthome/bedroom-2-node-01/telemetry";
constexpr char kCommandTopic[] = "smarthome/bedroom-2-node-01/command";
constexpr char kStateTopic[] = "smarthome/bedroom-2-node-01/state";

// Default scaffold pins. Replace after hardware mapping is known.
constexpr uint8_t kRelayPin = 4;
constexpr uint8_t kLedPin = 16;
constexpr uint8_t kTouchPin = 12;
constexpr uint8_t kBuzzerPin = 5;

constexpr bool kRelayActiveHigh = true;
constexpr bool kLedActiveHigh = true;
constexpr bool kTouchActiveHigh = true;
constexpr bool kBuzzerActiveHigh = true;

constexpr uint32_t kTelemetryIntervalMs = 5000;
constexpr uint32_t kTouchDebounceMs = 80;
constexpr int32_t kTimezoneOffsetSeconds = 7 * 60 * 60;
constexpr uint8_t kLedBreathStartHour = 22;
constexpr uint8_t kLedBreathEndHour = 6;
constexpr uint32_t kLedBreathPeriodMs = 4200;

}  // namespace NodeConfig
