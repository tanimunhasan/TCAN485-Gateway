#include "udp_forwarder.h"

#include <WiFiUdp.h>

#include "udp_config.h"
#include "wifi_manager.h"

namespace {

WiFiUDP udp;

}  // namespace

UdpForwarder::UdpForwarder(const WifiManager &wifi) : wifi_(wifi) {}

bool UdpForwarder::send(const uint8_t *data, size_t length) {
  if (length == 0) {
    return true;
  }
  if (!UdpConfig::kEnabled) {
    Serial.println("\nUDP forwarding is disabled.");
    return false;
  }
  if (!wifi_.isConnected()) {
    Serial.println("\nUDP forwarding skipped: Wi-Fi is disconnected.");
    return false;
  }
  if (udp.beginPacket(UdpConfig::kNodeRedHost, UdpConfig::kNodeRedPort) != 1) {
    Serial.println("\nUDP forwarding failed: cannot start packet.");
    return false;
  }
  if (udp.write(data, length) != length) {
    udp.endPacket();
    Serial.println("\nUDP forwarding failed: incomplete packet write.");
    return false;
  }
  if (udp.endPacket() != 1) {
    Serial.println("\nUDP forwarding failed: packet send error.");
    return false;
  }

  Serial.printf("\nUDP forwarded %u bytes to %s:%u\n",
                static_cast<unsigned>(length), UdpConfig::kNodeRedHost,
                UdpConfig::kNodeRedPort);
  return true;
}
