#pragma once

#include <Arduino.h>

class WifiManager;

class UdpForwarder {
 public:
  explicit UdpForwarder(const WifiManager &wifi);

  bool send(const uint8_t *data, size_t length);

 private:
  const WifiManager &wifi_;
};
