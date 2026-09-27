#pragma once

#include <Arduino.h>

class Rs485Transport;

class ControllerCommandSender {
 public:
  explicit ControllerCommandSender(Rs485Transport &rs485);

  bool send(const char *command);
  void setDestinationAddress(uint8_t address);
  uint8_t destinationAddress() const;

  bool isAwaitingResponse() const;
  uint32_t requestSentAtMs() const;
  void completeResponse();
  void cancelResponse();

 private:
  Rs485Transport &rs485_;
  uint8_t destinationAddress_{0x01};
  bool awaitingResponse_{false};
  uint32_t requestSentAtMs_{0};
};
