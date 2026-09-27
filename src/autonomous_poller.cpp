#include "autonomous_poller.h"

#include <cstdio>

#include "autonomous_config.h"
#include "clock_service.h"
#include "controller_command_sender.h"
#include "wifi_manager.h"

namespace {

constexpr uint8_t kFirstPageSize = AUTONOMOUS_FIRST_PAGE_SIZE;
constexpr uint8_t kMaxPageSize = AUTONOMOUS_MAX_PAGE_SIZE;
constexpr uint32_t kIntervalMs = AUTONOMOUS_INTERVAL_MS;
constexpr uint32_t kResponseTimeoutMs = AUTONOMOUS_RESPONSE_TIMEOUT_MS;

static_assert(kFirstPageSize >= 1 && kFirstPageSize <= kMaxPageSize,
              "Autonomous page size range is invalid");
static_assert(kMaxPageSize <= 25, "Controller supports at most 25 log records");

}  // namespace

AutonomousPoller::AutonomousPoller(ControllerCommandSender &controller,
                                   const WifiManager &wifi,
                                   const ClockService &clock)
    : controller_(controller), wifi_(wifi), clock_(clock),
      pageSize_(kFirstPageSize) {}

void AutonomousPoller::begin() {
  lastDispatchMs_ = millis();
  Serial.printf("Firmware mode: autonomous. Polling L=1h through L=%uh every %lu ms.\n",
                kMaxPageSize, static_cast<unsigned long>(kIntervalMs));
}

void AutonomousPoller::service() {
  if (!wifi_.isConnected()) {
    ready_ = false;
    printWaitReason(WaitReason::Wifi);
    return;
  }
  if (!clock_.hasValidTime()) {
    ready_ = false;
    printWaitReason(WaitReason::Time);
    return;
  }
  reportedWaitReason_ = WaitReason::None;

  const uint32_t now = millis();
  if (!ready_) {
    ready_ = true;
    lastDispatchMs_ = now;
    Serial.println("Autonomous polling is ready; first request in 10 seconds.");
    return;
  }
  if (controller_.isAwaitingResponse()) {
    if (static_cast<uint32_t>(now - controller_.requestSentAtMs()) >=
        kResponseTimeoutMs) {
      controller_.cancelResponse();
      Serial.println("Autonomous poll timed out waiting for an RS485 response.");
    } else {
      return;
    }
  }

  if (static_cast<uint32_t>(now - lastDispatchMs_) < kIntervalMs) {
    return;
  }

  char command[8];
  snprintf(command, sizeof(command), "L=%uh", pageSize_);
  if (controller_.send(command)) {
    lastDispatchMs_ = now;
    advancePageSize();
  }
}

void AutonomousPoller::printWaitReason(WaitReason reason) {
  if (reportedWaitReason_ == reason) {
    return;
  }
  reportedWaitReason_ = reason;
  if (reason == WaitReason::Wifi) {
    Serial.println("Autonomous polling is waiting for Wi-Fi.");
  } else if (reason == WaitReason::Time) {
    Serial.println("Autonomous polling is waiting for NTP UTC time.");
  }
}

void AutonomousPoller::advancePageSize() {
  pageSize_ = pageSize_ == kMaxPageSize
                  ? kFirstPageSize
                  : static_cast<uint8_t>(pageSize_ + 1);
}
