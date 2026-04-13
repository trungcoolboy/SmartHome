#pragma once

namespace NodeConfig {
constexpr char kNodeId[] = "esp12s-01";

constexpr char kWifiSsid[] = "Ludwig van Beethoven";
constexpr char kWifiPassword[] = "Anhtrung123";

constexpr char kMqttHost[] = "192.168.1.253";
constexpr uint16_t kMqttPort = 1883;
constexpr char kMqttUsername[] = "";
constexpr char kMqttPassword[] = "";

constexpr char kAvailabilityTopic[] = "smarthome/esp12s-01/availability";
constexpr char kTelemetryTopic[] = "smarthome/esp12s-01/telemetry";
constexpr char kCommandTopic[] = "smarthome/esp12s-01/command";
constexpr char kStateTopic[] = "smarthome/esp12s-01/state";
}
