#include "device_management_client.h"

#if DEVICE_MANAGEMENT_ENABLED

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <time.h>

#include "autonomous_poller.h"
#include "clock_service.h"
#include "device_management_config.h"
#include "device_management_credentials.h"
#include "firmware_version.h"
#include "wifi_manager.h"

namespace {

constexpr char kPreferencesNamespace[] = "dm_ota";
constexpr char kPendingKey[] = "pending";
constexpr char kDeploymentKey[] = "deploy_id";
constexpr char kTargetKey[] = "target_ver";
constexpr uint32_t kWorkerIdleMs = 100UL;
constexpr uint32_t kDisconnectedDelayMs = 1000UL;
constexpr uint32_t kWorkerStackBytes = 16384UL;

bool timeReached(uint32_t now, uint32_t target) {
  return target == 0 || static_cast<int32_t>(now - target) >= 0;
}

uint32_t boundedInterval(uint32_t seconds, uint32_t fallbackMs) {
  if (seconds == 0 || seconds > UINT32_MAX / 1000UL) {
    return fallbackMs;
  }
  const uint32_t milliseconds = seconds * 1000UL;
  return constrain(milliseconds, DeviceManagementConfig::kMinimumIntervalMs,
                   DeviceManagementConfig::kMaximumIntervalMs);
}

const char *resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_SW:
      return "software-reset";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_DEEPSLEEP:
      return "deep-sleep";
    default:
      return "unknown";
  }
}

}  // namespace

DeviceManagementClient::DeviceManagementClient(
    const WifiManager &wifi, const ClockService &clock,
    const AutonomousPoller &poller)
    : wifi_(wifi), clock_(clock), poller_(poller) {}

void DeviceManagementClient::begin() {
  enabled_ = DeviceManagementCredentials::kEnabled;
  if (!enabled_) {
    Serial.println(
        "Self-hosted management disabled in "
        "include/device_management_credentials.h.");
    return;
  }

  const String serverUrl(DeviceManagementCredentials::kServerUrl);
  if ((!serverUrl.startsWith("https://") &&
       !serverUrl.startsWith("http://")) ||
      DeviceManagementCredentials::kDeviceId[0] == '\0' ||
      DeviceManagementCredentials::kDeviceToken[0] == '\0') {
    enabled_ = false;
    Serial.println("Self-hosted management disabled: configuration invalid.");
    return;
  }
  if (serverUrl.indexOf("localhost") >= 0 ||
      serverUrl.indexOf("127.0.0.1") >= 0) {
    enabled_ = false;
    Serial.println(
        "Self-hosted management disabled: use the server LAN IP, not "
        "localhost.");
    return;
  }
  if (serverUrl.startsWith("http://") &&
      !DeviceManagementCredentials::kAllowInsecureHttp) {
    enabled_ = false;
    Serial.println(
        "Self-hosted management disabled: insecure HTTP is not allowed.");
    return;
  }
  if (serverUrl.startsWith("https://") &&
      DeviceManagementCredentials::kRootCaPem[0] == '\0') {
    enabled_ = false;
    Serial.println(
        "Self-hosted management disabled: HTTPS CA certificate is empty.");
    return;
  }

  restorePendingConfirmation();
  const BaseType_t created = xTaskCreatePinnedToCore(
      taskEntry, "device_management", kWorkerStackBytes, this, 1,
      &taskHandle_, 0);
  if (created != pdPASS) {
    enabled_ = false;
    taskHandle_ = nullptr;
    Serial.println("Self-hosted management task could not be started.");
    return;
  }

  Serial.println("Self-hosted management enabled; waiting for Wi-Fi.");
}

void DeviceManagementClient::service() {
  // Network and flash work runs in the dedicated task. The main-loop hook is
  // intentionally non-blocking so controller and terminal service continue.
}

void DeviceManagementClient::taskEntry(void *context) {
  static_cast<DeviceManagementClient *>(context)->taskLoop();
}

void DeviceManagementClient::taskLoop() {
  for (;;) {
    if (!enabled_ || !wifi_.isConnected()) {
      vTaskDelay(pdMS_TO_TICKS(kDisconnectedDelayMs));
      continue;
    }

    const String serverUrl(DeviceManagementCredentials::kServerUrl);
    if (serverUrl.startsWith("https://") && !clock_.hasValidTime()) {
      vTaskDelay(pdMS_TO_TICKS(kDisconnectedDelayMs));
      continue;
    }

    if (poller_.controllerResponsePending()) {
      vTaskDelay(pdMS_TO_TICKS(kWorkerIdleMs));
      continue;
    }

    const uint32_t now = millis();
    if (pendingConfirmation_ && timeReached(now, nextHeartbeatMs_)) {
      if (pendingTargetVersion_ == FirmwareVersion::kApplicationVersion) {
        if (sendHeartbeat("confirmed", pendingDeploymentId_, 100)) {
          clearPendingConfirmation();
          nextHeartbeatMs_ = millis() + heartbeatIntervalMs_;
          resetBackoff();
        } else {
          scheduleFailure(nextHeartbeatMs_, 0);
        }
      } else {
        const bool reported = postOtaEvent(
            pendingDeploymentId_, "failed", 100, pendingTargetVersion_,
            "version_mismatch",
            "Running firmware version does not match the installed target");
        if (reported) {
          clearPendingConfirmation();
        } else {
          scheduleFailure(nextHeartbeatMs_, 0);
        }
      }
      vTaskDelay(pdMS_TO_TICKS(kWorkerIdleMs));
      continue;
    }

    if (timeReached(now, nextHeartbeatMs_)) {
      if (sendHeartbeat()) {
        nextHeartbeatMs_ = millis() + heartbeatIntervalMs_;
        resetBackoff();
      } else {
        scheduleFailure(nextHeartbeatMs_, 0);
      }
    }

    if (!pendingConfirmation_ && timeReached(millis(), nextOtaPollMs_) &&
        !poller_.controllerResponsePending()) {
      if (pollOtaAssignment()) {
        nextOtaPollMs_ = millis() + otaPollIntervalMs_;
        resetBackoff();
      } else {
        scheduleFailure(nextOtaPollMs_, 0);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(kWorkerIdleMs));
  }
}

bool DeviceManagementClient::sendHeartbeat(
    const char *otaState, const String &deploymentId, uint8_t progressPercent,
    const char *errorCode, const char *errorMessage) {
  JsonDocument document;
  document["timestamp"] = utcTimestamp();

  JsonObject firmware = document["firmware"].to<JsonObject>();
  firmware["version"] = FirmwareVersion::kApplicationVersion;
  firmware["buildId"] = __DATE__ " " __TIME__;

  JsonObject hardware = document["hardware"].to<JsonObject>();
  hardware["model"] = DeviceManagementConfig::kHardwareModel;
  hardware["revision"] = DeviceManagementConfig::kHardwareRevision;

  JsonObject system = document["system"].to<JsonObject>();
  system["uptimeSeconds"] = millis() / 1000UL;
  system["freeMemoryBytes"] = ESP.getFreeHeap();
  system["resetReason"] = resetReasonName();

  JsonObject network = document["network"].to<JsonObject>();
  network["connected"] = wifi_.isConnected();
  network["type"] = "wifi";
  network["signalDbm"] = wifi_.rssi();
  network["ip"] = WiFi.localIP().toString();

  JsonObject clock = document["clock"].to<JsonObject>();
  clock["valid"] = clock_.hasValidTime();
  if (clock_.hasValidTime()) {
    clock["utcEpoch"] = clock_.epoch();
  } else {
    clock["utcEpoch"] = nullptr;
  }

  const char *healthResult = poller_.lastHealthCheckResult();
  JsonObject health = document["health"].to<JsonObject>();
  health["state"] = strcmp(healthResult, "passed") == 0
                        ? "ok"
                        : (strcmp(healthResult, "failed") == 0 ? "warning"
                                                               : "unknown");
  health["message"] = healthResult;

  JsonObject reported = document["reported"].to<JsonObject>();
  reported["runMode"] = DeviceManagementConfig::kRunMode;
  reported["controllerR3Confirmed"] =
      poller_.lastControllerModeConfirmedR3();
  reported["lastHealthCheckResult"] = healthResult;
  reported["lastHealthCheckUptimeSeconds"] =
      poller_.lastHealthCheckAtMs() / 1000UL;
  reported["lastHealthCheckUtcEpoch"] = poller_.lastHealthCheckEpoch();
  reported["hasSuccessfulLogResponse"] =
      poller_.hasSuccessfulLogResponse();
  reported["lastLogResponseUptimeSeconds"] =
      poller_.lastSuccessfulLogResponseAtMs() / 1000UL;
  reported["lastLogResponseUtcEpoch"] =
      poller_.lastSuccessfulLogResponseEpoch();
  reported["consecutiveLogFailures"] = poller_.consecutiveLogFailures();

  JsonObject ota = document["ota"].to<JsonObject>();
  if (deploymentId.length() > 0) {
    ota["deploymentId"] = deploymentId;
  } else {
    ota["deploymentId"] = nullptr;
  }
  ota["state"] = otaState;
  ota["progressPercent"] = progressPercent;
  ota["errorCode"] = errorCode;
  ota["errorMessage"] = errorMessage;

  String requestBody;
  serializeJson(document, requestBody);
  int statusCode = 0;
  String responseBody;
  if (!requestJson("POST", "/api/v1/device/heartbeat", requestBody,
                   statusCode, responseBody) ||
      statusCode < 200 || statusCode >= 300) {
    if (statusCode == HTTP_CODE_UNAUTHORIZED || statusCode == HTTP_CODE_FORBIDDEN) {
      Serial.println(
          "Self-hosted management credential was rejected; retrying slowly.");
      retryBackoffMs_ = DeviceManagementConfig::kUnauthorizedRetryMs;
    }
    return false;
  }

  JsonDocument response;
  if (deserializeJson(response, responseBody)) {
    Serial.println("Self-hosted heartbeat response was invalid JSON.");
    return false;
  }
  heartbeatIntervalMs_ = boundedInterval(
      response["heartbeatIntervalSeconds"] | 30U,
      DeviceManagementConfig::kDefaultHeartbeatIntervalMs);
  otaPollIntervalMs_ = boundedInterval(
      response["otaPollIntervalSeconds"] | 60U,
      DeviceManagementConfig::kDefaultOtaPollIntervalMs);
  Serial.println("Self-hosted management heartbeat accepted.");
  return true;
}

bool DeviceManagementClient::pollOtaAssignment() {
  int statusCode = 0;
  String responseBody;
  if (!requestJson("GET", "/api/v1/device/ota/assignment", String(),
                   statusCode, responseBody) ||
      statusCode < 200 || statusCode >= 300) {
    if (statusCode == HTTP_CODE_UNAUTHORIZED || statusCode == HTTP_CODE_FORBIDDEN) {
      retryBackoffMs_ = DeviceManagementConfig::kUnauthorizedRetryMs;
    }
    return false;
  }

  JsonDocument response;
  if (deserializeJson(response, responseBody)) {
    Serial.println("Self-hosted OTA assignment response was invalid JSON.");
    return false;
  }
  if (!(response["available"] | false)) {
    return true;
  }

  DeviceOtaAssignment assignment;
  assignment.deploymentId = response["deploymentId"] | "";
  JsonObject firmware = response["firmware"].as<JsonObject>();
  assignment.targetVersion = firmware["version"] | "";
  assignment.hardwareModel = firmware["hardwareModel"] | "";
  assignment.hardwareRevision = firmware["hardwareRevision"] | "";
  assignment.channel = firmware["channel"] | "";
  assignment.downloadUrl = firmware["downloadUrl"] | "";
  assignment.sha256 = firmware["sha256"] | "";
  assignment.sizeBytes = firmware["sizeBytes"] | 0U;

  if (assignment.deploymentId.length() == 0 ||
      assignment.targetVersion.length() == 0 ||
      assignment.downloadUrl.length() == 0 || assignment.sizeBytes == 0 ||
      assignment.sha256.length() != 64) {
    Serial.println("Self-hosted OTA assignment was incomplete.");
    return false;
  }

  const bool revisionMatches =
      assignment.hardwareRevision.length() == 0 ||
      assignment.hardwareRevision == DeviceManagementConfig::kHardwareRevision;
  if (assignment.hardwareModel != DeviceManagementConfig::kHardwareModel ||
      !revisionMatches || assignment.channel != DeviceManagementConfig::kChannel) {
    return postOtaEvent(
        assignment.deploymentId, "failed", 0, assignment.targetVersion,
        "compatibility_mismatch",
        "Assignment hardware, revision, or channel does not match this device");
  }
  if (assignment.sizeBytes > updater_.otaPartitionSize()) {
    return postOtaEvent(
        assignment.deploymentId, "failed", 0, assignment.targetVersion,
        "image_too_large", "Firmware does not fit the inactive OTA partition");
  }

  return handleOtaAssignment(assignment);
}

bool DeviceManagementClient::handleOtaAssignment(
    const DeviceOtaAssignment &assignment) {
  Serial.print("Self-hosted OTA assignment received for version ");
  Serial.println(assignment.targetVersion);

  if (!postOtaEvent(assignment.deploymentId, "downloading", 0,
                    assignment.targetVersion)) {
    return false;
  }

  const DeviceOtaDownloadResult download = updater_.downloadAndVerify(
      absoluteUrl(assignment.downloadUrl), DeviceManagementCredentials::kDeviceId,
      DeviceManagementCredentials::kDeviceToken,
      DeviceManagementCredentials::kAllowInsecureHttp,
      DeviceManagementCredentials::kRootCaPem, assignment.sizeBytes,
      assignment.sha256);
  if (!download.success) {
    Serial.print("Self-hosted OTA download failed: ");
    Serial.println(download.errorCode);
    return postOtaEvent(assignment.deploymentId, "failed", 0,
                        assignment.targetVersion, download.errorCode.c_str(),
                        download.errorMessage.c_str());
  }

  if (!postOtaEvent(assignment.deploymentId, "downloaded", 100,
                    assignment.targetVersion) ||
      !postOtaEvent(assignment.deploymentId, "verified", 100,
                    assignment.targetVersion) ||
      !postOtaEvent(assignment.deploymentId, "installing", 100,
                    assignment.targetVersion)) {
    updater_.abort();
    postOtaEvent(assignment.deploymentId, "failed", 100,
                 assignment.targetVersion, "event_reporting_failed",
                 "OTA state could not be recorded before installation");
    return false;
  }

  if (!savePendingConfirmation(assignment.deploymentId,
                               assignment.targetVersion)) {
    updater_.abort();
    return postOtaEvent(assignment.deploymentId, "failed", 100,
                        assignment.targetVersion, "nvs_write_failed",
                        "Could not save post-reboot confirmation state");
  }

  String errorCode;
  String errorMessage;
  if (!updater_.finalize(errorCode, errorMessage)) {
    clearPendingConfirmation();
    return postOtaEvent(assignment.deploymentId, "failed", 100,
                        assignment.targetVersion, errorCode.c_str(),
                        errorMessage.c_str());
  }

  if (!postOtaEvent(assignment.deploymentId, "rebooting", 100,
                    assignment.targetVersion)) {
    const bool restored = updater_.restoreRunningPartition();
    clearPendingConfirmation();
    Serial.println(restored
                       ? "OTA reboot event failed; previous boot partition restored."
                       : "OTA reboot event failed; boot partition restore failed.");
    postOtaEvent(assignment.deploymentId, "failed", 100,
                 assignment.targetVersion, "reboot_event_failed",
                 "Server did not accept the rebooting OTA state");
    return false;
  }

  Serial.println("Self-hosted OTA verified; rebooting into the new firmware.");
  delay(250);
  ESP.restart();
  return true;
}

bool DeviceManagementClient::postOtaEvent(
    const String &deploymentId, const char *state, uint8_t progressPercent,
    const String &targetVersion, const char *errorCode,
    const char *errorMessage) {
  JsonDocument document;
  document["timestamp"] = utcTimestamp();
  document["state"] = state;
  document["progressPercent"] = progressPercent;
  document["runningVersion"] = FirmwareVersion::kApplicationVersion;
  document["targetVersion"] = targetVersion;
  document["errorCode"] = errorCode;
  document["errorMessage"] = errorMessage;

  String requestBody;
  serializeJson(document, requestBody);
  int statusCode = 0;
  String responseBody;
  const String path =
      String("/api/v1/device/ota/") + deploymentId + "/events";
  return requestJson("POST", path, requestBody, statusCode, responseBody) &&
         statusCode >= 200 && statusCode < 300;
}

bool DeviceManagementClient::requestJson(
    const char *method, const String &path, const String &requestBody,
    int &statusCode, String &responseBody) {
  statusCode = 0;
  responseBody = String();
  const String url = absoluteUrl(path);

  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  bool opened = false;
  if (url.startsWith("https://")) {
    secureClient.setCACert(DeviceManagementCredentials::kRootCaPem);
    opened = http.begin(secureClient, url);
  } else if (url.startsWith("http://") &&
             DeviceManagementCredentials::kAllowInsecureHttp) {
    opened = http.begin(plainClient, url);
  }
  if (!opened) {
    Serial.println("Self-hosted management could not open the server URL.");
    return false;
  }

  http.setConnectTimeout(DeviceManagementConfig::kHttpConnectTimeoutMs);
  http.setTimeout(DeviceManagementConfig::kHttpResponseTimeoutMs);
  http.setReuse(false);
  http.addHeader("Authorization",
                 String("Bearer ") + DeviceManagementCredentials::kDeviceToken);
  http.addHeader("X-Device-ID", DeviceManagementCredentials::kDeviceId);

  if (strcmp(method, "GET") == 0) {
    statusCode = http.GET();
  } else {
    http.addHeader("Content-Type", "application/json");
    statusCode = http.POST(requestBody);
  }

  if (statusCode <= 0) {
    Serial.print("Self-hosted management transport error: ");
    Serial.println(HTTPClient::errorToString(statusCode));
    http.end();
    return false;
  }

  const int declaredLength = http.getSize();
  if (declaredLength >
      static_cast<int>(DeviceManagementConfig::kMaximumJsonResponseBytes)) {
    Serial.println("Self-hosted management response was too large.");
    http.end();
    return false;
  }
  if (statusCode != HTTP_CODE_NO_CONTENT) {
    responseBody = http.getString();
  }
  http.end();

  if (responseBody.length() >
      DeviceManagementConfig::kMaximumJsonResponseBytes) {
    responseBody = String();
    Serial.println("Self-hosted management response exceeded its size limit.");
    return false;
  }
  return true;
}

String DeviceManagementClient::absoluteUrl(const String &path) const {
  if (path.startsWith("http://") || path.startsWith("https://")) {
    return path;
  }
  String base(DeviceManagementCredentials::kServerUrl);
  while (base.endsWith("/")) {
    base.remove(base.length() - 1);
  }
  return path.startsWith("/") ? base + path : base + "/" + path;
}

String DeviceManagementClient::utcTimestamp() const {
  if (!clock_.hasValidTime()) {
    return "1970-01-01T00:00:00Z";
  }
  const time_t epoch = static_cast<time_t>(clock_.epoch());
  struct tm utcTime;
  gmtime_r(&epoch, &utcTime);
  char timestamp[21];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
  return String(timestamp);
}

void DeviceManagementClient::scheduleFailure(uint32_t &nextAttemptMs,
                                             int statusCode) {
  if (statusCode == HTTP_CODE_UNAUTHORIZED || statusCode == HTTP_CODE_FORBIDDEN) {
    retryBackoffMs_ = DeviceManagementConfig::kUnauthorizedRetryMs;
  }
  nextAttemptMs = millis() + retryBackoffMs_;
  retryBackoffMs_ =
      retryBackoffMs_ >= DeviceManagementConfig::kMaximumRetryMs / 2U
          ? DeviceManagementConfig::kMaximumRetryMs
          : retryBackoffMs_ * 2U;
}

void DeviceManagementClient::resetBackoff() {
  retryBackoffMs_ = DeviceManagementConfig::kInitialRetryMs;
}

void DeviceManagementClient::restorePendingConfirmation() {
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, true)) {
    return;
  }
  pendingConfirmation_ = preferences.getBool(kPendingKey, false);
  pendingDeploymentId_ = preferences.getString(kDeploymentKey, "");
  pendingTargetVersion_ = preferences.getString(kTargetKey, "");
  preferences.end();
  if (pendingConfirmation_ &&
      (pendingDeploymentId_.length() == 0 ||
       pendingTargetVersion_.length() == 0)) {
    clearPendingConfirmation();
  }
}

bool DeviceManagementClient::savePendingConfirmation(
    const String &deploymentId, const String &targetVersion) {
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, false)) {
    return false;
  }
  const bool saved =
      preferences.putString(kDeploymentKey, deploymentId) > 0 &&
      preferences.putString(kTargetKey, targetVersion) > 0 &&
      preferences.putBool(kPendingKey, true) > 0;
  preferences.end();
  if (saved) {
    pendingConfirmation_ = true;
    pendingDeploymentId_ = deploymentId;
    pendingTargetVersion_ = targetVersion;
  }
  return saved;
}

void DeviceManagementClient::clearPendingConfirmation() {
  Preferences preferences;
  if (preferences.begin(kPreferencesNamespace, false)) {
    preferences.clear();
    preferences.end();
  }
  pendingConfirmation_ = false;
  pendingDeploymentId_ = String();
  pendingTargetVersion_ = String();
}

#else

DeviceManagementClient::DeviceManagementClient(
    const WifiManager &wifi, const ClockService &clock,
    const AutonomousPoller &poller)
    : wifi_(wifi), clock_(clock), poller_(poller) {}

void DeviceManagementClient::begin() {}
void DeviceManagementClient::service() {}
void DeviceManagementClient::taskEntry(void *) {}

#endif
