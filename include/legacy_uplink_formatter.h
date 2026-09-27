#pragma once

#include <Arduino.h>
#include <Preferences.h>

class ClockService;
class WifiManager;

enum class UplinkFormatResult {
  Formatted,
  Ignored,
  Disabled,
  TimeNotReady,
  Rejected,
};

class LegacyUplinkFormatter {
 public:
  static constexpr size_t kMaxPayloadLength = 1600;

  LegacyUplinkFormatter(const WifiManager &wifi, const ClockService &clock);

  void begin();
  UplinkFormatResult format(const uint8_t *rawFrame, size_t rawLength,
                            char *output, size_t outputCapacity,
                            size_t &outputLength, const char *&detail);
  bool forwardRawDiagnostics() const;

 private:
  uint16_t nextMessageIndex();

  const WifiManager &wifi_;
  const ClockService &clock_;
  Preferences preferences_;
};
