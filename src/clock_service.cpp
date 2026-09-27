#include "clock_service.h"

#include <time.h>

#include "legacy_uplink_config.h"
#include "wifi_manager.h"

ClockService::ClockService(const WifiManager &wifi) : wifi_(wifi) {}

void ClockService::begin() {
  Serial.println("NTP clock is waiting for Wi-Fi.");
}

void ClockService::service() {
  if (valid_ || !wifi_.isConnected()) {
    return;
  }

  const uint32_t now = millis();
  if (!syncRequested_ ||
      static_cast<uint32_t>(now - lastRequestMs_) >= kRetryIntervalMs) {
    requestSync();
  }

  const time_t currentTime = time(nullptr);
  if (currentTime >= static_cast<time_t>(kMinimumValidEpoch)) {
    valid_ = true;
    Serial.printf("NTP clock synchronized: %lu UTC.\n",
                  static_cast<unsigned long>(currentTime));
  }
}

bool ClockService::hasValidTime() const {
  return valid_;
}

uint32_t ClockService::epoch() const {
  return valid_ ? static_cast<uint32_t>(time(nullptr)) : 0;
}

void ClockService::requestSync() {
  syncRequested_ = true;
  lastRequestMs_ = millis();
  Serial.println("Synchronizing UTC time with NTP.");
  configTime(LegacyUplinkConfig::kUtcOffsetSeconds,
             LegacyUplinkConfig::kDaylightOffsetSeconds,
             LegacyUplinkConfig::kNtpServer);
}
