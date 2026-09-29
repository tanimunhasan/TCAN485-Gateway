#pragma once

#include <Arduino.h>

class Rs485Transport;

class ControllerCommandSender {
 public:
  static constexpr size_t kResponseSnapshotCapacity = 192;

  explicit ControllerCommandSender(Rs485Transport &rs485);

  bool send(const char *command);
  void setDestinationAddress(uint8_t address);
  uint8_t destinationAddress() const;

  bool isAwaitingResponse() const;
  uint32_t requestSentAtMs() const;
  char activeCommandCode() const;
  void completeResponse(const uint8_t *frame, size_t frameLength,
                        bool overflowed);
  bool takeCompletedResponse(char expectedCommandCode);
  bool takeCompletedResponse(char expectedCommandCode, char *payload,
                             size_t payloadCapacity, size_t &payloadLength,
                             bool &truncated);
  void cancelResponse();

 private:
  Rs485Transport &rs485_;
  uint8_t destinationAddress_{0x01};
  bool awaitingResponse_{false};
  uint32_t requestSentAtMs_{0};
  char activeCommandCode_{'\0'};
  char completedPayload_[kResponseSnapshotCapacity]{};
  size_t completedPayloadLength_{0};
  char completedCommandCode_{'\0'};
  bool completedResponseReady_{false};
  bool completedPayloadTruncated_{false};
};
