#pragma once

#include <stdint.h>

namespace NodeConfig {

constexpr char kNodeId[] = "bathroom-1-node-01-discovery";

constexpr char kWifiSsid[] = "Ludwig van Beethoven";
constexpr char kWifiPassword[] = "Anhtrung123";

constexpr char kMqttHost[] = "192.168.1.253";
constexpr uint16_t kMqttPort = 1883;
constexpr char kMqttUsername[] = "";
constexpr char kMqttPassword[] = "";

constexpr char kOtaHostname[] = "bathroom-1-node-01-discovery";
constexpr char kOtaPassword[] = "";

constexpr char kAvailabilityTopic[] = "smarthome/bathroom-1-node-01-discovery/availability";
constexpr char kTelemetryTopic[] = "smarthome/bathroom-1-node-01-discovery/telemetry";
constexpr char kCommandTopic[] = "smarthome/bathroom-1-node-01-discovery/command";
constexpr char kStateTopic[] = "smarthome/bathroom-1-node-01-discovery/state";

constexpr uint8_t kCandidatePins[] = {0, 2, 4, 5, 12, 13, 14, 15, 16};
constexpr size_t kCandidatePinCount = sizeof(kCandidatePins) / sizeof(kCandidatePins[0]);

constexpr uint32_t kTelemetryIntervalMs = 5000;
constexpr uint32_t kInputSampleIntervalMs = 30;
constexpr uint8_t kProbeToggleCount = 8;
constexpr uint32_t kProbeToggleIntervalMs = 250;

}  // namespace NodeConfig
