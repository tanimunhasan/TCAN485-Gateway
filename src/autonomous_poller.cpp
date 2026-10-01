#include "autonomous_poller.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "autonomous_config.h"
#include "clock_service.h"
#include "controller_command_sender.h"
#include "wifi_manager.h"

namespace {

constexpr uint8_t kLogPageSize = AUTONOMOUS_LOG_PAGE_SIZE;
constexpr uint32_t kIntervalMs = AUTONOMOUS_INTERVAL_MS;
constexpr uint32_t kResponseTimeoutMs = AUTONOMOUS_RESPONSE_TIMEOUT_MS;
constexpr uint32_t kControllerRetryMs = AUTONOMOUS_CONTROLLER_RETRY_MS;
constexpr uint8_t kControllerMaxAttempts =
    AUTONOMOUS_CONTROLLER_MAX_ATTEMPTS;
constexpr uint32_t kModeAuditIntervalMs =
    AUTONOMOUS_MODE_AUDIT_INTERVAL_MS;
constexpr uint32_t kDegradedRetryMs = AUTONOMOUS_DEGRADED_RETRY_MS;
constexpr uint8_t kLogFailuresBeforeAudit =
    AUTONOMOUS_LOG_FAILURES_BEFORE_AUDIT;

static_assert(kLogPageSize >= 1 && kLogPageSize <= 25,
              "Controller supports a log page size from 1 to 25");
static_assert(kControllerMaxAttempts >= 1,
              "Controller maintenance needs at least one attempt");
static_assert(kLogFailuresBeforeAudit >= 1,
              "Log failure threshold needs at least one failure");

}  // namespace

AutonomousPoller::AutonomousPoller(ControllerCommandSender &controller,
                                   const WifiManager &wifi,
                                   const ClockService &clock)
    : controller_(controller), wifi_(wifi), clock_(clock) {}

void AutonomousPoller::begin() {
  lastDispatchMs_ = millis();
  Serial.printf(
      "Firmware mode: autonomous. Polling fixed L=%uh every %lu ms.\n",
      kLogPageSize, static_cast<unsigned long>(kIntervalMs));
  Serial.println(
      "Production controller supervision: enforce R=3, then synchronize UTC.");
}

void AutonomousPoller::service() {
  const uint32_t now = millis();

  if (controllerState_ != ControllerState::Ready) {
    serviceControllerManagement(now);
    return;
  }

  if (logRequestPending_) {
    serviceLogPolling(now);
    return;
  }

  if (controller_.isAwaitingResponse()) {
    return;
  }

  if (modeAuditIntervalMs_ > 0 &&
      static_cast<uint32_t>(now - lastModeAuditMs_) >=
          modeAuditIntervalMs_) {
    beginModeAudit(now, "scheduled production-mode audit");
    serviceControllerManagement(now);
    return;
  }

  const bool timeRetryReady =
      !timeSyncBackoffActive_ ||
      static_cast<uint32_t>(now - lastTimeSyncFailureMs_) >=
          kDegradedRetryMs;
  if (timeSyncDue_ && modeConfirmedNormal_ && clock_.hasValidTime() &&
      timeRetryReady) {
    timeSyncBackoffActive_ = false;
    modeAuditInProgress_ = false;
    controllerState_ = ControllerState::NeedTimeSync;
    serviceControllerManagement(now);
    return;
  }

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

  if (!ready_) {
    ready_ = true;
    lastDispatchMs_ = now;
    Serial.printf("Autonomous polling is ready; first request in %lu seconds.\n",
                  static_cast<unsigned long>(kIntervalMs / 1000UL));
    return;
  }

  serviceLogPolling(now);
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

void AutonomousPoller::beginModeAudit(uint32_t now, const char *reason) {
  Serial.printf("Starting controller mode audit: %s.\n", reason);
  controllerState_ = ControllerState::NeedModeQuery;
  retryState_ = ControllerState::NeedModeQuery;
  controllerStateStartedMs_ = now;
  modeAuditAttempts_ = 0;
  modeAuditInProgress_ = true;
  modeConfirmedNormal_ = false;
  timeSyncDue_ = true;
}

void AutonomousPoller::serviceControllerManagement(uint32_t now) {
  char payload[ControllerCommandSender::kResponseSnapshotCapacity];
  size_t payloadLength = 0;
  bool payloadTruncated = false;
  uint8_t mode = 0;

  switch (controllerState_) {
    case ControllerState::NeedModeQuery:
      ++modeAuditAttempts_;
      if (!controller_.send("R=?")) {
        handleModeAuditFailure(now, "could not send R=?");
        return;
      }
      controllerState_ = ControllerState::WaitModeQuery;
      controllerStateStartedMs_ = now;
      Serial.printf("Controller mode query attempt %u of %u.\n",
                    modeAuditAttempts_, kControllerMaxAttempts);
      return;

    case ControllerState::WaitModeQuery:
      if (controller_.isAwaitingResponse()) {
        if (responseTimedOut(now)) {
          controller_.cancelResponse();
          handleModeAuditFailure(now, "R=? response timed out");
        }
        return;
      }
      if (!controller_.takeCompletedResponse(
              'R', payload, sizeof(payload), payloadLength,
              payloadTruncated) ||
          payloadTruncated || !parseOperationMode(payload, mode)) {
        handleModeAuditFailure(now, "R=? response was invalid");
        return;
      }
      if (mode == 3) {
        Serial.println("Controller confirmed R=3 - OPM_NORMAL; mode unchanged.");
        modeConfirmedNormal_ = true;
        if (clock_.hasValidTime()) {
          controllerState_ = ControllerState::NeedTimeSync;
        } else {
          finishModeAudit(now);
        }
      } else {
        Serial.printf(
            "Controller reported R=%u; production policy requires R=3.\n",
            mode);
        controllerState_ = ControllerState::NeedSetNormal;
      }
      return;

    case ControllerState::NeedSetNormal:
      if (!controller_.send("R=3")) {
        handleModeAuditFailure(now, "could not send R=3");
        return;
      }
      controllerState_ = ControllerState::WaitSetNormal;
      controllerStateStartedMs_ = now;
      return;

    case ControllerState::WaitSetNormal:
      if (controller_.isAwaitingResponse()) {
        if (responseTimedOut(now)) {
          controller_.cancelResponse();
          handleModeAuditFailure(now, "R=3 response timed out");
        }
        return;
      }
      if (!controller_.takeCompletedResponse('R')) {
        handleModeAuditFailure(now, "R=3 response was invalid");
        return;
      }
      controllerState_ = ControllerState::NeedModeConfirmation;
      return;

    case ControllerState::NeedModeConfirmation:
      if (!controller_.send("R=?")) {
        handleModeAuditFailure(now, "could not confirm R=3");
        return;
      }
      controllerState_ = ControllerState::WaitModeConfirmation;
      controllerStateStartedMs_ = now;
      return;

    case ControllerState::WaitModeConfirmation:
      if (controller_.isAwaitingResponse()) {
        if (responseTimedOut(now)) {
          controller_.cancelResponse();
          handleModeAuditFailure(now, "R=3 confirmation timed out");
        }
        return;
      }
      if (!controller_.takeCompletedResponse(
              'R', payload, sizeof(payload), payloadLength,
              payloadTruncated) ||
          payloadTruncated || !parseOperationMode(payload, mode) || mode != 3) {
        handleModeAuditFailure(now,
                               "controller did not confirm OPM_NORMAL");
        return;
      }
      Serial.println("Controller transition to R=3 - OPM_NORMAL confirmed.");
      modeConfirmedNormal_ = true;
      if (clock_.hasValidTime()) {
        controllerState_ = ControllerState::NeedTimeSync;
      } else {
        finishModeAudit(now);
      }
      return;

    case ControllerState::NeedTimeSync: {
      if (!clock_.hasValidTime()) {
        if (modeAuditInProgress_) {
          finishModeAudit(now);
        } else {
          controllerState_ = ControllerState::Ready;
        }
        return;
      }

      char command[24];
      snprintf(command, sizeof(command), "T=%lu",
               static_cast<unsigned long>(clock_.epoch()));
      if (!controller_.send(command)) {
        handleTimeSyncFailure(now, "could not send T=<epoch>");
        return;
      }
      controllerState_ = ControllerState::WaitTimeSync;
      controllerStateStartedMs_ = now;
      return;
    }

    case ControllerState::WaitTimeSync:
      if (controller_.isAwaitingResponse()) {
        if (responseTimedOut(now)) {
          controller_.cancelResponse();
          handleTimeSyncFailure(now, "T=<epoch> response timed out");
        }
        return;
      }
      if (!controller_.takeCompletedResponse('T')) {
        handleTimeSyncFailure(now, "T=<epoch> response was invalid");
        return;
      }
      timeSyncDue_ = false;
      timeSyncBackoffActive_ = false;
      Serial.println("Controller accepted UTC synchronization.");
      if (modeAuditInProgress_) {
        finishModeAudit(now);
      } else {
        controllerState_ = ControllerState::Ready;
      }
      return;

    case ControllerState::RetryDelay:
      if (static_cast<uint32_t>(now - controllerStateStartedMs_) >=
          kControllerRetryMs) {
        controllerState_ = retryState_;
      }
      return;

    case ControllerState::Ready:
      return;
  }
}

void AutonomousPoller::handleModeAuditFailure(uint32_t now,
                                              const char *reason) {
  if (modeAuditAttempts_ < kControllerMaxAttempts) {
    Serial.printf(
        "Controller mode audit failed: %s; retrying in %lu seconds.\n",
        reason, static_cast<unsigned long>(kControllerRetryMs / 1000UL));
    retryState_ = ControllerState::NeedModeQuery;
    controllerState_ = ControllerState::RetryDelay;
    controllerStateStartedMs_ = now;
    return;
  }

  Serial.printf(
      "Controller mode audit unavailable after %u attempts: %s. Log polling "
      "will continue; mode audit retries in %lu seconds.\n",
      kControllerMaxAttempts, reason,
      static_cast<unsigned long>(kDegradedRetryMs / 1000UL));
  modeConfirmedNormal_ = false;
  modeAuditInProgress_ = false;
  controllerState_ = ControllerState::Ready;
  lastModeAuditMs_ = now;
  modeAuditIntervalMs_ = kDegradedRetryMs;
  completeStartup(now);
}

void AutonomousPoller::handleTimeSyncFailure(uint32_t now,
                                             const char *reason) {
  Serial.printf(
      "Controller UTC synchronization failed: %s; background retry in %lu "
      "seconds.\n",
      reason, static_cast<unsigned long>(kDegradedRetryMs / 1000UL));
  timeSyncDue_ = true;
  timeSyncBackoffActive_ = true;
  lastTimeSyncFailureMs_ = now;
  if (modeAuditInProgress_) {
    finishModeAudit(now);
  } else {
    controllerState_ = ControllerState::Ready;
  }
}

void AutonomousPoller::finishModeAudit(uint32_t now) {
  controllerState_ = ControllerState::Ready;
  modeAuditInProgress_ = false;
  modeAuditAttempts_ = 0;
  lastModeAuditMs_ = now;
  modeAuditIntervalMs_ = kModeAuditIntervalMs;
  consecutiveLogFailures_ = 0;
  completeStartup(now);
}

void AutonomousPoller::completeStartup(uint32_t now) {
  if (startupComplete_) {
    return;
  }
  startupComplete_ = true;
  ready_ = false;
  lastDispatchMs_ = now;
  Serial.println("Controller supervision startup pass complete.");
}

void AutonomousPoller::serviceLogPolling(uint32_t now) {
  if (logRequestPending_) {
    if (controller_.isAwaitingResponse()) {
      if (static_cast<uint32_t>(now - controller_.requestSentAtMs()) >=
          kResponseTimeoutMs) {
        controller_.cancelResponse();
        logRequestPending_ = false;
        handleLogFailure(now, "L= response timed out");
      }
      return;
    }

    logRequestPending_ = false;
    if (!controller_.takeCompletedResponse('L')) {
      handleLogFailure(now, "L= response was invalid");
      return;
    }

    consecutiveLogFailures_ = 0;
    return;
  }

  if (controller_.isAwaitingResponse() ||
      static_cast<uint32_t>(now - lastDispatchMs_) < kIntervalMs) {
    return;
  }

  char command[8];
  snprintf(command, sizeof(command), "L=%uh", kLogPageSize);
  if (controller_.send(command)) {
    logRequestPending_ = true;
    lastDispatchMs_ = now;
  }
}

void AutonomousPoller::handleLogFailure(uint32_t now, const char *reason) {
  ++consecutiveLogFailures_;
  lastDispatchMs_ = now;
  Serial.printf("Autonomous log poll failed (%u/%u): %s.\n",
                consecutiveLogFailures_, kLogFailuresBeforeAudit, reason);
  if (consecutiveLogFailures_ >= kLogFailuresBeforeAudit) {
    consecutiveLogFailures_ = 0;
    beginModeAudit(now, "repeated log response failures");
  }
}

bool AutonomousPoller::responseTimedOut(uint32_t now) const {
  return static_cast<uint32_t>(now - controllerStateStartedMs_) >=
         kResponseTimeoutMs;
}

bool AutonomousPoller::parseOperationMode(const char *payload,
                                          uint8_t &mode) {
  if (payload == nullptr) {
    return false;
  }

  const char *marker = strstr(payload, "R=");
  if (marker == nullptr) {
    return false;
  }

  errno = 0;
  char *end = nullptr;
  const unsigned long parsed = strtoul(marker + 2, &end, 10);
  if (errno != 0 || end == marker + 2 || parsed > 0xFF) {
    return false;
  }

  mode = static_cast<uint8_t>(parsed);
  return true;
}
