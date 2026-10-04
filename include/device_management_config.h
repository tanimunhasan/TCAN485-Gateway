#pragma once

#include <stdint.h>

namespace DeviceManagementConfig {

// These values must exactly match the device and firmware metadata in the
// management platform.
constexpr char kHardwareModel[] = "ESP32";
constexpr char kHardwareRevision[] = "V1";
constexpr char kChannel[] = "production";
constexpr char kRunMode[] = "autonomous";

constexpr uint32_t kDefaultHeartbeatIntervalMs = 30000UL;
constexpr uint32_t kDefaultOtaPollIntervalMs = 60000UL;
constexpr uint32_t kMinimumIntervalMs = 5000UL;
constexpr uint32_t kMaximumIntervalMs = 3600000UL;
constexpr uint32_t kInitialRetryMs = 5000UL;
constexpr uint32_t kMaximumRetryMs = 300000UL;
constexpr uint32_t kUnauthorizedRetryMs = 300000UL;
constexpr uint32_t kHttpConnectTimeoutMs = 5000UL;
constexpr uint32_t kHttpResponseTimeoutMs = 15000UL;
constexpr size_t kMaximumJsonResponseBytes = 16384U;

}  // namespace DeviceManagementConfig
