#pragma once

#include <Arduino.h>

#include "rs485_transport.h"
class ControllerCommandSender;
class LegacyUplinkFormatter;
class UdpForwarder;

class Terminal {
 public:
  Terminal(Rs485Transport &rs485, ControllerCommandSender &controller,
           UdpForwarder &udpForwarder,
           LegacyUplinkFormatter &legacyFormatter);

  void begin();
  void service();

 private:
  enum class LineEnding {
    None,
    CR,
    LF,
    CRLF,
  };

  static constexpr size_t kInputCapacity = 256;
  static constexpr size_t kReceiveBufferCapacity = 2112;
  static constexpr uint32_t kReceiveGapMs = 25;

  void serviceConsole();
  void serviceRs485();
  void serviceUdpForwarder();
  void processLine();
  void sendText(const char *text);
  void sendHex(const char *text);
  void sendControllerCommand(const char *command);
  void printHelp() const;
  void printStatus() const;
  void printControllerCommands() const;
  void eraseLastCharacter();
  void eraseLine();
  void writeLineEnding();
  const char *lineEndingName() const;

  Rs485Transport &rs485_;
  ControllerCommandSender &controller_;
  UdpForwarder &udpForwarder_;
  LegacyUplinkFormatter &legacyFormatter_;
  char input_[kInputCapacity]{};
  size_t inputLength_{0};
  bool inputOverflow_{false};
  char previousTerminator_{'\0'};
  LineEnding lineEnding_{LineEnding::CRLF};
  uint8_t receiveBuffer_[kReceiveBufferCapacity]{};
  size_t receiveLength_{0};
  bool receiveOverflow_{false};
  uint32_t lastReceiveByteMs_{0};
};
