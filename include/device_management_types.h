#pragma once

#include <Arduino.h>

struct DeviceOtaAssignment {
  String deploymentId;
  String targetVersion;
  String hardwareModel;
  String hardwareRevision;
  String channel;
  String downloadUrl;
  String sha256;
  size_t sizeBytes{0};
};

struct DeviceOtaDownloadResult {
  bool success{false};
  String errorCode;
  String errorMessage;
};
