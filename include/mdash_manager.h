#pragma once

#include <stdint.h>

class AutonomousPoller;
class ClockService;
class WifiManager;

class MDashManager {
 public:
  MDashManager(const WifiManager &wifi, const ClockService &clock,
               const AutonomousPoller &poller);

  void begin();
  void service();

 private:
  void publishHealth(uint32_t now);

  static constexpr uint32_t kHealthPublishIntervalMs = 60000UL;

  const WifiManager &wifi_;
  const ClockService &clock_;
  const AutonomousPoller &poller_;
  bool enabled_{false};
  bool started_{false};
  bool cloudConnected_{false};
  uint32_t lastHealthPublishMs_{0};
};
