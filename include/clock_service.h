#pragma once

#include <Arduino.h>

class WifiManager;

class ClockService {
 public:
  explicit ClockService(const WifiManager &wifi);

  void begin();
  void service();

  bool hasValidTime() const;
  uint32_t epoch() const;

 private:
  void requestSync();

  static constexpr uint32_t kRetryIntervalMs = 30000;
  static constexpr uint32_t kMinimumValidEpoch = 1704067200;

  const WifiManager &wifi_;
  bool syncRequested_{false};
  bool valid_{false};
  uint32_t lastRequestMs_{0};
};
