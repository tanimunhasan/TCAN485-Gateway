#pragma once

#include <Arduino.h>

#include "device_management_types.h"
#include "device_ota_updater.h"

#ifndef DEVICE_MANAGEMENT_ENABLED
#define DEVICE_MANAGEMENT_ENABLED 0
#endif

class AutonomousPoller;
class ClockService;
class WifiManager;

class DeviceManagementClient {
 public:
  DeviceManagementClient(const WifiManager &wifi, const ClockService &clock,
                         const AutonomousPoller &poller);

  void begin();
  void service();

 private:
  static void taskEntry(void *context);
  void taskLoop();

  bool sendHeartbeat(const char *otaState = "idle",
                     const String &deploymentId = String(),
                     uint8_t progressPercent = 0,
                     const char *errorCode = nullptr,
                     const char *errorMessage = nullptr);
  bool pollOtaAssignment();
  bool handleOtaAssignment(const DeviceOtaAssignment &assignment);
  bool postOtaEvent(const String &deploymentId, const char *state,
                    uint8_t progressPercent, const String &targetVersion,
                    const char *errorCode = nullptr,
                    const char *errorMessage = nullptr);
  bool requestJson(const char *method, const String &path,
                   const String &requestBody, int &statusCode,
                   String &responseBody);
  String absoluteUrl(const String &path) const;
  String utcTimestamp() const;
  void scheduleFailure(uint32_t &nextAttemptMs, int statusCode);
  void resetBackoff();
  void restorePendingConfirmation();
  bool savePendingConfirmation(const String &deploymentId,
                               const String &targetVersion);
  void clearPendingConfirmation();

  const WifiManager &wifi_;
  const ClockService &clock_;
  const AutonomousPoller &poller_;
  DeviceOtaUpdater updater_;
  TaskHandle_t taskHandle_{nullptr};
  bool enabled_{false};
  bool pendingConfirmation_{false};
  String pendingDeploymentId_;
  String pendingTargetVersion_;
  uint32_t heartbeatIntervalMs_{30000UL};
  uint32_t otaPollIntervalMs_{60000UL};
  uint32_t retryBackoffMs_{5000UL};
  uint32_t nextHeartbeatMs_{0};
  uint32_t nextOtaPollMs_{0};
};
