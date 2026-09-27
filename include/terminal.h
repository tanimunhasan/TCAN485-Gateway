#pragma once

#include <Arduino.h>

#include "rs485_transport.h"

class Terminal {
 public:
  explicit Terminal(Rs485Transport &rs485);

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

  void serviceConsole();
  void serviceRs485();
  void processLine();
  void sendText(const char *text);
  void sendHex(const char *text);
  void printHelp() const;
  void printStatus() const;
  void eraseLastCharacter();
  void eraseLine();
  void writeLineEnding();
  const char *lineEndingName() const;

  Rs485Transport &rs485_;
  char input_[kInputCapacity]{};
  size_t inputLength_{0};
  bool inputOverflow_{false};
  char previousTerminator_{'\0'};
  LineEnding lineEnding_{LineEnding::CRLF};
};
