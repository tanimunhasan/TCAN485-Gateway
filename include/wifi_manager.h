#pragma once

#include <Arduino.h>

class WifiManager {
 public:
  void begin();
  void service();

  bool isConnected() const;
  int32_t rssi() const;

 private:
  void startConnection();

  static constexpr uint32_t kReconnectIntervalMs = 10000;

  bool enabled_{false};
  bool connected_{false};
  uint32_t lastConnectionAttemptMs_{0};
};
