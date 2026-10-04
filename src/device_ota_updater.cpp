#include "device_ota_updater.h"

#ifndef DEVICE_MANAGEMENT_ENABLED
#define DEVICE_MANAGEMENT_ENABLED 0
#endif

#if DEVICE_MANAGEMENT_ENABLED

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include "device_management_config.h"

namespace {

constexpr size_t kDownloadBufferBytes = 4096U;

DeviceOtaDownloadResult failure(const char *code, const String &message) {
  DeviceOtaDownloadResult result;
  result.errorCode = code;
  result.errorMessage = message;
  return result;
}

}  // namespace

DeviceOtaDownloadResult DeviceOtaUpdater::downloadAndVerify(
    const String &url, const char *deviceId, const char *deviceToken,
    bool allowInsecureHttp, const char *rootCaPem, size_t expectedSize,
    const String &expectedSha256) {
  abort();

  if (expectedSize == 0 || expectedSize > otaPartitionSize()) {
    return failure("image_too_large",
                   "Firmware does not fit the inactive OTA partition");
  }
  if (expectedSha256.length() != 64) {
    return failure("invalid_manifest", "Firmware SHA-256 is invalid");
  }

  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  bool opened = false;

  if (url.startsWith("https://")) {
    if (rootCaPem == nullptr || rootCaPem[0] == '\0') {
      return failure("missing_ca_certificate",
                     "HTTPS server CA certificate is not configured");
    }
    secureClient.setCACert(rootCaPem);
    opened = http.begin(secureClient, url);
  } else if (url.startsWith("http://") && allowInsecureHttp) {
    opened = http.begin(plainClient, url);
  } else {
    return failure("insecure_transport_rejected",
                   "Firmware URL is not an allowed HTTPS URL");
  }

  if (!opened) {
    return failure("download_connection_failed",
                   "Could not open the firmware download URL");
  }

  http.setConnectTimeout(DeviceManagementConfig::kHttpConnectTimeoutMs);
  http.setTimeout(DeviceManagementConfig::kHttpResponseTimeoutMs);
  http.setReuse(false);
  http.addHeader("Authorization", String("Bearer ") + deviceToken);
  http.addHeader("X-Device-ID", deviceId);

  const int statusCode = http.GET();
  if (statusCode != HTTP_CODE_OK) {
    http.end();
    return failure("download_rejected",
                   String("Firmware download returned HTTP ") + statusCode);
  }

  const int contentLength = http.getSize();
  if (contentLength < 0 || static_cast<size_t>(contentLength) != expectedSize) {
    http.end();
    return failure("size_mismatch",
                   "Firmware Content-Length does not match the assignment");
  }

  runningPartition_ = esp_ota_get_running_partition();
  if (!Update.begin(expectedSize, U_FLASH)) {
    const String message = String("OTA staging failed: ") + Update.errorString();
    http.end();
    return failure("ota_begin_failed", message);
  }
  active_ = true;

  uint8_t *buffer = static_cast<uint8_t *>(malloc(kDownloadBufferBytes));
  if (buffer == nullptr) {
    abort();
    http.end();
    return failure("out_of_memory", "Could not allocate the OTA download buffer");
  }

  mbedtls_sha256_context shaContext;
  mbedtls_sha256_init(&shaContext);
  if (mbedtls_sha256_starts_ret(&shaContext, 0) != 0) {
    free(buffer);
    abort();
    http.end();
    mbedtls_sha256_free(&shaContext);
    return failure("sha256_initialization_failed",
                   "Could not initialize SHA-256 verification");
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t received = 0;
  uint32_t lastDataMs = millis();
  String transferError;

  while (received < expectedSize) {
    const int available = stream->available();
    if (available > 0) {
      const size_t remaining = expectedSize - received;
      const size_t requested =
          min(static_cast<size_t>(available), min(kDownloadBufferBytes, remaining));
      const size_t count = stream->readBytes(buffer, requested);
      if (count == 0) {
        transferError = "Firmware stream stopped before completion";
        break;
      }
      if (Update.write(buffer, count) != count) {
        transferError = String("OTA flash write failed: ") + Update.errorString();
        break;
      }
      if (mbedtls_sha256_update_ret(&shaContext, buffer, count) != 0) {
        transferError = "SHA-256 update failed";
        break;
      }
      received += count;
      lastDataMs = millis();
      continue;
    }

    if (!http.connected()) {
      transferError = "Firmware connection closed before completion";
      break;
    }
    if (static_cast<uint32_t>(millis() - lastDataMs) >=
        DeviceManagementConfig::kHttpResponseTimeoutMs) {
      transferError = "Firmware download timed out";
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  uint8_t digest[32];
  const int finishResult = mbedtls_sha256_finish_ret(&shaContext, digest);
  mbedtls_sha256_free(&shaContext);
  free(buffer);
  http.end();

  if (transferError.length() > 0 || received != expectedSize) {
    abort();
    return failure("download_interrupted",
                   transferError.length() > 0
                       ? transferError
                       : String("Firmware byte count was incomplete"));
  }
  if (finishResult != 0) {
    abort();
    return failure("sha256_finalization_failed",
                   "Could not finalize SHA-256 verification");
  }

  char actualSha256[65];
  for (size_t index = 0; index < sizeof(digest); ++index) {
    snprintf(&actualSha256[index * 2], 3, "%02x", digest[index]);
  }
  actualSha256[64] = '\0';

  if (!expectedSha256.equalsIgnoreCase(actualSha256)) {
    abort();
    return failure("checksum_mismatch",
                   "Downloaded firmware SHA-256 did not match the assignment");
  }

  DeviceOtaDownloadResult result;
  result.success = true;
  return result;
}

bool DeviceOtaUpdater::finalize(String &errorCode, String &errorMessage) {
  if (!active_) {
    errorCode = "ota_not_staged";
    errorMessage = "No verified OTA image is staged";
    return false;
  }
  if (!Update.end(true)) {
    errorCode = "ota_finalize_failed";
    errorMessage = String("OTA finalization failed: ") + Update.errorString();
    active_ = false;
    return false;
  }
  active_ = false;
  return true;
}

bool DeviceOtaUpdater::restoreRunningPartition() {
  if (runningPartition_ == nullptr) {
    return false;
  }
  return esp_ota_set_boot_partition(
             static_cast<const esp_partition_t *>(runningPartition_)) == ESP_OK;
}

void DeviceOtaUpdater::abort() {
  if (active_) {
    Update.abort();
    active_ = false;
  }
}

size_t DeviceOtaUpdater::otaPartitionSize() const {
  const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
  return partition == nullptr ? 0U : partition->size;
}

#else

DeviceOtaDownloadResult DeviceOtaUpdater::downloadAndVerify(
    const String &, const char *, const char *, bool, const char *, size_t,
    const String &) {
  return DeviceOtaDownloadResult{};
}

bool DeviceOtaUpdater::finalize(String &, String &) { return false; }
bool DeviceOtaUpdater::restoreRunningPartition() { return false; }
void DeviceOtaUpdater::abort() {}
size_t DeviceOtaUpdater::otaPartitionSize() const { return 0U; }

#endif
