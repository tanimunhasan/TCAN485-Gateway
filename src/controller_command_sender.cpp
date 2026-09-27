#include "controller_command_sender.h"

#include <cstring>

#include "controller_protocol.h"
#include "rs485_transport.h"

ControllerCommandSender::ControllerCommandSender(Rs485Transport &rs485)
    : rs485_(rs485) {}

bool ControllerCommandSender::send(const char *command) {
  char frame[ControllerProtocol::kMaxFrameLength];
  if (!ControllerProtocol::buildFrame(destinationAddress_, command, frame,
                                      sizeof(frame))) {
    Serial.println("Invalid controller command. Use /commands for valid codes.");
    return false;
  }

  const size_t frameLength = strlen(frame);
  rs485_.write(reinterpret_cast<const uint8_t *>(frame), frameLength);
  rs485_.flush();
  awaitingResponse_ = destinationAddress_ != 0;
  requestSentAtMs_ = millis();

  Serial.print("TX: ");
  Serial.print(frame);
  return true;
}

void ControllerCommandSender::setDestinationAddress(uint8_t address) {
  destinationAddress_ = address;
}

uint8_t ControllerCommandSender::destinationAddress() const {
  return destinationAddress_;
}

bool ControllerCommandSender::isAwaitingResponse() const {
  return awaitingResponse_;
}

uint32_t ControllerCommandSender::requestSentAtMs() const {
  return requestSentAtMs_;
}

void ControllerCommandSender::completeResponse() {
  awaitingResponse_ = false;
}

void ControllerCommandSender::cancelResponse() {
  awaitingResponse_ = false;
}
