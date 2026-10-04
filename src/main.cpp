#include <Arduino.h>
#include "autonomous_poller.h"

#include "clock_service.h"
#include "controller_command_sender.h"
#include "device_management_client.h"
#include "firmware_mode.h"
#include "legacy_uplink_formatter.h"
#include "mdash_manager.h"
#include "rs485_transport.h"
#include "terminal.h"
#include "udp_forwarder.h"
#include "wifi_manager.h"

#ifndef MDASH_ENABLED
#define MDASH_ENABLED 0
#endif

#ifndef DEVICE_MANAGEMENT_ENABLED
#define DEVICE_MANAGEMENT_ENABLED 0
#endif

#if MDASH_ENABLED && DEVICE_MANAGEMENT_ENABLED
#error "Enable either mDash or self-hosted management, not both"
#endif

namespace {

Rs485Transport rs485;
ControllerCommandSender controller(rs485);
WifiManager wifi;
ClockService gatewayClock(wifi);
UdpForwarder udpForwarder(wifi);
LegacyUplinkFormatter legacyFormatter(wifi, gatewayClock);
Terminal terminal(rs485, controller, udpForwarder, legacyFormatter);
#if FIRMWARE_RUN_MODE == FIRMWARE_MODE_AUTONOMOUS
AutonomousPoller autonomousPoller(controller, wifi, gatewayClock);
#if DEVICE_MANAGEMENT_ENABLED
DeviceManagementClient deviceManagement(wifi, gatewayClock, autonomousPoller);
#elif MDASH_ENABLED
MDashManager mdash(wifi, gatewayClock, autonomousPoller);
#endif
#endif

}  // namespace

void setup() {
  rs485.begin();
  terminal.begin();
  wifi.begin();
  gatewayClock.begin();
  legacyFormatter.begin();
#if FIRMWARE_RUN_MODE == FIRMWARE_MODE_AUTONOMOUS
  autonomousPoller.begin();
#if DEVICE_MANAGEMENT_ENABLED
  deviceManagement.begin();
#elif MDASH_ENABLED
  mdash.begin();
#else
  Serial.println("No remote management plane enabled for this build.");
#endif
#else
  Serial.println("Firmware mode: manual terminal.");
#endif
}

void loop() {
  wifi.service();
  gatewayClock.service();
  terminal.service();
#if FIRMWARE_RUN_MODE == FIRMWARE_MODE_AUTONOMOUS
  autonomousPoller.service();
#if DEVICE_MANAGEMENT_ENABLED
  deviceManagement.service();
#elif MDASH_ENABLED
  mdash.service();
#endif
#endif
}
