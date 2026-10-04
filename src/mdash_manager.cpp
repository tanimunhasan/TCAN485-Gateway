#include "mdash_manager.h"

#include <Arduino.h>

#include "autonomous_poller.h"
#include "clock_service.h"
#include "firmware_version.h"
#include "wifi_manager.h"

#ifndef MDASH_ENABLED
#define MDASH_ENABLED 0
#endif

#if MDASH_ENABLED
#define MDASH_APP_NAME "RS485_Operation"
#include <WiFi.h>
#include <mDash.h>

#include "mdash_credentials.h"
#endif

MDashManager::MDashManager(const WifiManager &wifi, const ClockService &clock,
                           const AutonomousPoller &poller)
    : wifi_(wifi), clock_(clock), poller_(poller) {}

void MDashManager::begin() {
#if MDASH_ENABLED
  enabled_ = MDashCredentials::kEnabled;
  if (!enabled_) {
    Serial.println(
        "mDash management disabled in include/mdash_credentials.h.");
    return;
  }

  if (MDashCredentials::kDevicePassword[0] == '\0') {
    enabled_ = false;
    Serial.println("mDash management disabled: device password is empty.");
    return;
  }

  Serial.println("mDash management enabled; waiting for Wi-Fi.");
#else
  Serial.println("mDash management is not enabled for this build.");
#endif
}

void MDashManager::service() {
#if MDASH_ENABLED
  if (!enabled_) {
    return;
  }

  if (!started_) {
    if (!wifi_.isConnected() || poller_.controllerResponsePending()) {
      return;
    }

    // mDash owns its cloud task and reconnect logic. Starting it only after
    // the first Wi-Fi connection avoids waiting for network setup here.
    mDashBegin(MDashCredentials::kDevicePassword);
    started_ = true;
    Serial.println("mDash management client started.");
    return;
  }

  const bool nowCloudConnected =
      mDashGetState() == MDASH_STATE_CLOUD_CONNECTED;
  if (nowCloudConnected != cloudConnected_) {
    cloudConnected_ = nowCloudConnected;
    if (cloudConnected_) {
      lastHealthPublishMs_ = 0;
    }
    Serial.println(cloudConnected_ ? "mDash cloud connected."
                                   : "mDash cloud disconnected.");
  }

  if (!cloudConnected_ || poller_.controllerResponsePending()) {
    return;
  }

  const uint32_t now = millis();
  if (lastHealthPublishMs_ == 0 ||
      static_cast<uint32_t>(now - lastHealthPublishMs_) >=
          kHealthPublishIntervalMs) {
    publishHealth(now);
  }
#endif
}

void MDashManager::publishHealth(uint32_t now) {
#if MDASH_ENABLED
  const bool wifiConnected = wifi_.isConnected();
  const String ipAddress =
      wifiConnected ? WiFi.localIP().toString() : String("0.0.0.0");

  const int sent = mDashShadowUpdate(
      "{%Q:{%Q:{%Q:{"
      "%Q:%Q,%Q:%lu,%Q:%lu,"
      "%Q:%s,%Q:%d,%Q:%Q,"
      "%Q:%s,%Q:%lu,"
      "%Q:%Q,"
      "%Q:%s,%Q:%Q,%Q:%lu,%Q:%lu,"
      "%Q:%s,%Q:%lu,%Q:%lu,%Q:%u"
      "}}}}",
      "state", "reported", "gateway", "firmware_version",
      FirmwareVersion::kApplicationVersion, "uptime_s",
      static_cast<unsigned long>(now / 1000UL), "free_heap_bytes",
      static_cast<unsigned long>(ESP.getFreeHeap()), "wifi_connected",
      wifiConnected ? "true" : "false", "wifi_rssi_dbm", wifi_.rssi(),
      "wifi_ip", ipAddress.c_str(), "ntp_valid",
      clock_.hasValidTime() ? "true" : "false", "utc_epoch",
      static_cast<unsigned long>(clock_.epoch()), "run_mode", "autonomous",
      "controller_r3_confirmed",
      poller_.lastControllerModeConfirmedR3() ? "true" : "false",
      "last_health_check_result", poller_.lastHealthCheckResult(),
      "last_health_check_uptime_s",
      static_cast<unsigned long>(poller_.lastHealthCheckAtMs() / 1000UL),
      "last_health_check_epoch",
      static_cast<unsigned long>(poller_.lastHealthCheckEpoch()),
      "has_successful_log_response",
      poller_.hasSuccessfulLogResponse() ? "true" : "false",
      "last_log_response_uptime_s",
      static_cast<unsigned long>(poller_.lastSuccessfulLogResponseAtMs() /
                                 1000UL),
      "last_log_response_epoch",
      static_cast<unsigned long>(poller_.lastSuccessfulLogResponseEpoch()),
      "consecutive_log_failures", poller_.consecutiveLogFailures());

  lastHealthPublishMs_ = now;
  if (sent != 1) {
    Serial.println("mDash health update was not accepted; will retry later.");
  }
#else
  (void) now;
#endif
}
