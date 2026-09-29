#include "controller_command_sender.h"

#include <algorithm>
#include <cctype>
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
  activeCommandCode_ = static_cast<char>(
      std::toupper(static_cast<unsigned char>(command[0])));
  completedResponseReady_ = false;
  completedPayloadLength_ = 0;
  completedPayloadTruncated_ = false;

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

char ControllerCommandSender::activeCommandCode() const {
  return activeCommandCode_;
}

void ControllerCommandSender::completeResponse(const uint8_t *frame,
                                                size_t frameLength,
                                                bool overflowed) {
  const uint8_t *payload = nullptr;
  size_t payloadLength = 0;
  if (awaitingResponse_ && !overflowed &&
      ControllerProtocol::parseResponseFrame(
          frame, frameLength, destinationAddress_, payload, payloadLength)) {
    const size_t copyLength =
        std::min(payloadLength, sizeof(completedPayload_) - 1);
    memcpy(completedPayload_, payload, copyLength);
    completedPayload_[copyLength] = '\0';
    completedPayloadLength_ = copyLength;
    completedCommandCode_ = activeCommandCode_;
    completedPayloadTruncated_ =
        payloadLength >= sizeof(completedPayload_);
    completedResponseReady_ = true;
  }
  awaitingResponse_ = false;
}

bool ControllerCommandSender::takeCompletedResponse(char expectedCommandCode) {
  if (!completedResponseReady_ ||
      completedCommandCode_ != expectedCommandCode) {
    return false;
  }

  completedResponseReady_ = false;
  return true;
}

bool ControllerCommandSender::takeCompletedResponse(
    char expectedCommandCode, char *payload, size_t payloadCapacity,
    size_t &payloadLength, bool &truncated) {
  if (!completedResponseReady_ ||
      completedCommandCode_ != expectedCommandCode || payload == nullptr ||
      payloadCapacity == 0) {
    return false;
  }

  const size_t copyLength =
      std::min(completedPayloadLength_, payloadCapacity - 1);
  memcpy(payload, completedPayload_, copyLength);
  payload[copyLength] = '\0';
  payloadLength = copyLength;
  truncated = completedPayloadTruncated_ ||
              completedPayloadLength_ >= payloadCapacity;
  completedResponseReady_ = false;
  return true;
}

void ControllerCommandSender::cancelResponse() {
  awaitingResponse_ = false;
  activeCommandCode_ = '\0';
  completedResponseReady_ = false;
  completedPayloadLength_ = 0;
  completedPayloadTruncated_ = false;
}
