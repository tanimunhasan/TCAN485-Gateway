#pragma once

namespace DeviceManagementCredentials {

// Copy this file to device_management_credentials.h. The real file is ignored
// by Git. Paste the three values shown once by the platform when provisioning
// or rotating a device credential.
constexpr bool kEnabled = false;

// For an isolated LAN bench test, use the management PC's LAN address, never
// localhost (for example, http://192.168.1.50:8080).
constexpr char kServerUrl[] = "http://YOUR_MANAGEMENT_PC_IP:8080";
constexpr char kDeviceId[] = "dev_YOUR_DEVICE_ID";
constexpr char kDeviceToken[] = "dvt_YOUR_ONE_TIME_DEVICE_TOKEN";

// Plain HTTP is allowed only for the isolated local bench configuration.
// Production deployments must set this false and use an HTTPS URL.
constexpr bool kAllowInsecureHttp = true;

// Public CA certificate used to validate an HTTPS management server. This is
// trust material, not a secret. It may remain empty for the local HTTP bench.
constexpr char kRootCaPem[] = "";

}  // namespace DeviceManagementCredentials
