#include <Arduino.h>
#include "autonomous_poller.h"

#include "clock_service.h"
#include "controller_command_sender.h"
#include "firmware_mode.h"
#include "legacy_uplink_formatter.h"
#include "rs485_transport.h"
#include "terminal.h"
#include "udp_forwarder.h"
#include "wifi_manager.h"

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
#endif
}
