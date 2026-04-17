#pragma once

#include <stdint.h>

namespace NodeConfig {

constexpr char kNodeId[] = "bedroom-2-node-02";

constexpr char kWifiSsid[] = "Ludwig van Beethoven";
constexpr char kWifiPassword[] = "Anhtrung123";

constexpr char kMqttHost[] = "192.168.1.253";
constexpr uint16_t kMqttPort = 1883;
constexpr char kMqttUsername[] = "";
constexpr char kMqttPassword[] = "";

constexpr char kOtaHostname[] = "bedroom-2-node-02";
constexpr char kOtaPassword[] = "";

constexpr char kAvailabilityTopic[] = "smarthome/bedroom-2-node-02/availability";
constexpr char kTelemetryTopic[] = "smarthome/bedroom-2-node-02/telemetry";
constexpr char kCommandTopic[] = "smarthome/bedroom-2-node-02/command";
constexpr char kStateTopic[] = "smarthome/bedroom-2-node-02/state";

constexpr uint8_t kTouchPin = 16;
constexpr uint8_t kLedPin = 14;

constexpr bool kTouchActiveHigh = true;
constexpr bool kLedActiveHigh = true;

constexpr uint32_t kTelemetryIntervalMs = 5000;
constexpr uint32_t kTouchDebounceMs = 80;

}  // namespace NodeConfig
