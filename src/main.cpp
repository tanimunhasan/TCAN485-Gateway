#include <Arduino.h>

#include "rs485_transport.h"
#include "terminal.h"
#include "udp_forwarder.h"
#include "wifi_manager.h"

namespace {

Rs485Transport rs485;
WifiManager wifi;
UdpForwarder udpForwarder(wifi);
Terminal terminal(rs485, udpForwarder);

}  // namespace

void setup() {
  rs485.begin();
  terminal.begin();
  wifi.begin();
}

void loop() {
  terminal.service();
  wifi.service();
}
