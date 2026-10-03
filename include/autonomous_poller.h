#pragma once

#include <Arduino.h>

class ClockService;
class ControllerCommandSender;
class WifiManager;

class AutonomousPoller {
 public:
  AutonomousPoller(ControllerCommandSender &controller, const WifiManager &wifi,
                   const ClockService &clock);

  void begin();
  void service();

 private:
  enum class ControllerState {
    NeedModeQuery,
    WaitModeQuery,
    WaitSetNormal,
    NeedModeConfirmation,
    WaitModeConfirmation,
    NeedTimeSync,
    WaitTimeSync,
    NeedTimeConfirmation,
    WaitTimeConfirmation,
    RetryDelay,
    Ready,
  };

  enum class WaitReason {
    None,
    Wifi,
    Time,
  };

  void printWaitReason(WaitReason reason);
  void beginHealthCheck(uint32_t now, const char *reason);
  void serviceControllerManagement(uint32_t now);
  void handleHealthCheckFailure(uint32_t now, const char *reason);
  void handleTimeSyncFailure(uint32_t now, const char *reason);
  void finishHealthCheck(uint32_t now);
  void finishTimeCheck(uint32_t now);
  void completeStartup(uint32_t now);
  void serviceLogPolling(uint32_t now);
  void handleLogFailure(uint32_t now, const char *reason);
  bool responseTimedOut(uint32_t now) const;
  static bool parseOperationMode(const char *payload, uint8_t &mode);
  static bool parseControllerEpoch(const char *payload, uint32_t &epoch);
  static uint32_t epochDifference(uint32_t first, uint32_t second);

  ControllerCommandSender &controller_;
  const WifiManager &wifi_;
  const ClockService &clock_;
  uint32_t lastDispatchMs_{0};
  bool ready_{false};
  WaitReason reportedWaitReason_{WaitReason::None};
  ControllerState controllerState_{ControllerState::NeedModeQuery};
  ControllerState retryState_{ControllerState::NeedModeQuery};
  uint32_t controllerStateStartedMs_{0};
  uint32_t lastHealthCheckMs_{0};
  uint32_t healthCheckIntervalMs_{0};
  uint32_t lastTimeSyncFailureMs_{0};
  uint8_t healthCheckAttempts_{0};
  uint8_t consecutiveLogFailures_{0};
  bool healthCheckInProgress_{true};
  bool modeConfirmedNormal_{false};
  bool timeSyncDue_{false};
  bool timeSyncBackoffActive_{false};
  bool startupComplete_{false};
  bool logRequestPending_{false};
};
