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
  enum class WaitReason {
    None,
    Wifi,
    Time,
  };

  void printWaitReason(WaitReason reason);
  void advancePageSize();

  ControllerCommandSender &controller_;
  const WifiManager &wifi_;
  const ClockService &clock_;
  uint8_t pageSize_{1};
  uint32_t lastDispatchMs_{0};
  bool ready_{false};
  WaitReason reportedWaitReason_{WaitReason::None};
};
