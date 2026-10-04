#pragma once

#include <Arduino.h>

#include "device_management_types.h"

class DeviceOtaUpdater {
 public:
  DeviceOtaDownloadResult downloadAndVerify(
      const String &url, const char *deviceId, const char *deviceToken,
      bool allowInsecureHttp, const char *rootCaPem, size_t expectedSize,
      const String &expectedSha256);

  bool finalize(String &errorCode, String &errorMessage);
  bool restoreRunningPartition();
  void abort();
  size_t otaPartitionSize() const;

 private:
  bool active_{false};
  const void *runningPartition_{nullptr};
};
