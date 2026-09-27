#include "wifi_manager.h"

#include <WiFi.h>

#include "wifi_credentials.h"

void WifiManager::begin() 
{
  enabled_ = WifiCredentials::kEnabled;
  if (!enabled_) {
    Serial.println("Wi-Fi disabled. Set credentials in include/wifi_credentials.h.");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  startConnection();
}

void WifiManager::service() {
  if (!enabled_) {
    return;
  }

  const bool nowConnected = WiFi.status() == WL_CONNECTED;
  if (nowConnected) {
    if (!connected_) {
      connected_ = true;
      Serial.print("Wi-Fi connected. IP address: ");
      Serial.println(WiFi.localIP());
    }
    return;
  }

  if (connected_) {
    connected_ = false;
    Serial.println("Wi-Fi connection lost.");
  }

  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - lastConnectionAttemptMs_) >=
      kReconnectIntervalMs) {
    startConnection();
  }
}

bool WifiManager::isConnected() const {
  return connected_;
}

void WifiManager::startConnection() {
  lastConnectionAttemptMs_ = millis();
  Serial.print("Connecting to Wi-Fi network: ");
  Serial.println(WifiCredentials::kSsid);

  WiFi.disconnect(false, false);
  WiFi.begin(WifiCredentials::kSsid, WifiCredentials::kPassword);
}
