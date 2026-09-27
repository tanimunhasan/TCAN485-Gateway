#pragma once

#include <Arduino.h>

class Rs485Transport {
 public:
  static constexpr uint32_t kDefaultBaud = 9600;

  void begin(uint32_t baud = kDefaultBaud);
  bool setBaud(uint32_t baud);

  size_t write(const uint8_t *data, size_t length);
  size_t write(uint8_t value);
  void flush();

  int available();
  int read();
  uint32_t baud() const;

 private:
  HardwareSerial serial_{1};
  uint32_t baud_{kDefaultBaud};
};
