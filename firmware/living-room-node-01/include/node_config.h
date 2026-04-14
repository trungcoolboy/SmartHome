#pragma once

#include <stdint.h>

namespace NodeConfig {

constexpr char kNodeId[] = "living-room-node-01";

constexpr char kWifiSsid[] = "Ludwig van Beethoven";
constexpr char kWifiPassword[] = "Anhtrung123";

constexpr char kMqttHost[] = "192.168.1.253";
constexpr uint16_t kMqttPort = 1883;
constexpr char kMqttUsername[] = "";
constexpr char kMqttPassword[] = "";

constexpr char kOtaHostname[] = "living-room-node-01";
constexpr char kOtaPassword[] = "";

constexpr char kAvailabilityTopic[] = "smarthome/living-room-node-01/availability";
constexpr char kTelemetryTopic[] = "smarthome/living-room-node-01/telemetry";
constexpr char kCommandTopic[] = "smarthome/living-room-node-01/command";
constexpr char kStateTopic[] = "smarthome/living-room-node-01/state";

constexpr uint8_t kRelayPin = 5;
constexpr uint8_t kLedPin = 4;
constexpr uint8_t kTouchPin = 16;

constexpr bool kRelayActiveHigh = true;
constexpr bool kLedActiveHigh = true;
constexpr bool kTouchActiveHigh = true;

constexpr uint32_t kTelemetryIntervalMs = 5000;
constexpr uint32_t kTouchDebounceMs = 80;

}  // namespace
