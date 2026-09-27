#include "terminal.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace {

constexpr uint32_t kUsbBaud = 9600;

bool startsWithCommand(const char *line, const char *command,
                       const char *&argument) {
  const size_t commandLength = strlen(command);
  if (strncasecmp(line, command, commandLength) != 0) {
    return false;
  }

  argument = line + commandLength;
  if (*argument == '\0') {
    return true;
  }
  if (!std::isspace(static_cast<unsigned char>(*argument))) {
    return false;
  }

  while (std::isspace(static_cast<unsigned char>(*argument))) {
    ++argument;
  }
  return true;
}

bool parseHexByte(const char *&cursor, uint8_t &value) {
  while (std::isspace(static_cast<unsigned char>(*cursor)) ||
         *cursor == ',' || *cursor == ':' || *cursor == '-') {
    ++cursor;
  }
  if (*cursor == '\0') {
    return false;
  }

  errno = 0;
  char *end = nullptr;
  const unsigned long parsed = strtoul(cursor, &end, 16);
  if (errno != 0 || end == cursor || parsed > 0xFF) {
    return false;
  }
  if (*end != '\0' && !std::isspace(static_cast<unsigned char>(*end)) &&
      *end != ',' && *end != ':' && *end != '-') {
    return false;
  }

  value = static_cast<uint8_t>(parsed);
  cursor = end;
  return true;
}

}  // namespace

Terminal::Terminal(Rs485Transport &rs485) : rs485_(rs485) {}

void Terminal::begin() {
  Serial.begin(kUsbBaud);
  delay(300);

  Serial.println();
  Serial.println("T-CAN485 raw RS485 terminal ready.");
  Serial.printf("USB monitor: %lu baud | RS485: %lu baud, 8N1\n",
                static_cast<unsigned long>(kUsbBaud),
                static_cast<unsigned long>(rs485_.baud()));
  Serial.println("Type text and press Enter to send it to RS485.");
  Serial.println("Backspace/Delete removes one character; Ctrl+U clears the line.");
  Serial.println("Type /help for local terminal commands.");
  Serial.print("> ");
}

void Terminal::service() {
  serviceRs485();
  serviceConsole();
}

void Terminal::serviceRs485() {
  while (rs485_.available() > 0) {
    const int received = rs485_.read();
    if (received >= 0) {
      // This intentionally writes raw data so the monitor behaves like a
      // normal serial terminal and shows exactly what arrived from RS485.
      Serial.write(static_cast<uint8_t>(received));
    }
  }
}

void Terminal::serviceConsole() {
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());

    if (value == '\r' || value == '\n') {
      // Serial terminals commonly send CRLF. Treat that as one Enter key.
      if (inputLength_ == 0 && previousTerminator_ != '\0' &&
          value != previousTerminator_) {
        previousTerminator_ = value;
        continue;
      }

      Serial.print("\r\n");
      if (inputOverflow_) {
        Serial.println("Input discarded: line is too long.");
      } else if (inputLength_ > 0) {
        input_[inputLength_] = '\0';
        processLine();
      }

      inputLength_ = 0;
      inputOverflow_ = false;
      previousTerminator_ = value;
      Serial.print("> ");
      continue;
    }

    previousTerminator_ = '\0';
    if (value == '\b' || value == 0x7F) {
      eraseLastCharacter();
    } else if (value == 0x15) {  // Ctrl+U
      eraseLine();
    } else if (std::isprint(static_cast<unsigned char>(value)) ||
               value == '\t') {
      if (inputLength_ < sizeof(input_) - 1) {
        input_[inputLength_++] = value;
        Serial.write(static_cast<uint8_t>(value));
      } else {
        inputOverflow_ = true;
        Serial.write('\a');
      }
    }
  }
}

void Terminal::processLine() {
  const char *argument = nullptr;

  if (input_[0] != '/') {
    sendText(input_);
    return;
  }

  const char *command = input_ + 1;
  if (startsWithCommand(command, "help", argument) && *argument == '\0') {
    printHelp();
  } else if (startsWithCommand(command, "status", argument) &&
             *argument == '\0') {
    printStatus();
  } else if (startsWithCommand(command, "send", argument) && *argument != '\0') {
    sendText(argument);
  } else if (startsWithCommand(command, "hex", argument) && *argument != '\0') {
    sendHex(argument);
  } else if (startsWithCommand(command, "baud", argument) && *argument != '\0') {
    errno = 0;
    char *end = nullptr;
    const unsigned long requestedBaud = strtoul(argument, &end, 10);
    while (end != nullptr && std::isspace(static_cast<unsigned char>(*end))) {
      ++end;
    }
    if (errno != 0 || end == argument || (end != nullptr && *end != '\0') ||
        !rs485_.setBaud(static_cast<uint32_t>(requestedBaud))) {
      Serial.println("Invalid baud rate. Use a value from 300 to 3000000.");
    } else {
      Serial.printf("RS485 baud changed to %lu.\n", requestedBaud);
    }
  } else if (startsWithCommand(command, "ending", argument) &&
             *argument != '\0') {
    if (strcasecmp(argument, "none") == 0) {
      lineEnding_ = LineEnding::None;
    } else if (strcasecmp(argument, "cr") == 0) {
      lineEnding_ = LineEnding::CR;
    } else if (strcasecmp(argument, "lf") == 0) {
      lineEnding_ = LineEnding::LF;
    } else if (strcasecmp(argument, "crlf") == 0) {
      lineEnding_ = LineEnding::CRLF;
    } else {
      Serial.println("Line ending must be none, cr, lf, or crlf.");
      return;
    }
    Serial.printf("RS485 text line ending: %s\n", lineEndingName());
  } else {
    Serial.println("Unknown local command. Type /help.");
  }
}

void Terminal::sendText(const char *text) {
  const size_t length = strlen(text);
  if (length > 0) {
    rs485_.write(reinterpret_cast<const uint8_t *>(text), length);
  }
  writeLineEnding();
  rs485_.flush();
}

void Terminal::sendHex(const char *text) {
  const char *cursor = text;
  size_t sent = 0;

  while (true) {
    while (std::isspace(static_cast<unsigned char>(*cursor)) ||
           *cursor == ',' || *cursor == ':' || *cursor == '-') {
      ++cursor;
    }
    if (*cursor == '\0') {
      break;
    }

    uint8_t value = 0;
    if (!parseHexByte(cursor, value)) {
      Serial.println("Invalid hex. Example: /hex 48 65 6C 6C 6F 0D 0A");
      return;
    }
    rs485_.write(value);
    ++sent;
  }

  if (sent == 0) {
    Serial.println("No hexadecimal bytes supplied.");
    return;
  }
  rs485_.flush();
}

void Terminal::printHelp() const {
  Serial.println("Normal text is sent to RS485 when Enter is pressed.");
  Serial.println("Backspace/Delete removes one typed character; Ctrl+U clears all.");
  Serial.println("Local commands:");
  Serial.println("  /help                   Show this help");
  Serial.println("  /status                 Show RS485 configuration");
  Serial.println("  /send <text>            Send text that begins with /");
  Serial.println("  /hex <bytes>            Send exact bytes, e.g. /hex 48 69 0D 0A");
  Serial.println("  /ending none|cr|lf|crlf Set suffix for normal text (default: crlf)");
  Serial.println("  /baud <rate>            Change RS485 baud rate");
}

void Terminal::printStatus() const {
  Serial.printf("USB monitor: %lu baud\n", static_cast<unsigned long>(kUsbBaud));
  Serial.printf("RS485: %lu baud, 8N1; text ending: %s\n",
                static_cast<unsigned long>(rs485_.baud()), lineEndingName());
}

void Terminal::eraseLastCharacter() {
  if (inputLength_ == 0) {
    return;
  }
  --inputLength_;
  Serial.print("\b \b");
}

void Terminal::eraseLine() {
  while (inputLength_ > 0) {
    eraseLastCharacter();
  }
}

void Terminal::writeLineEnding() {
  if (lineEnding_ == LineEnding::CR || lineEnding_ == LineEnding::CRLF) {
    rs485_.write('\r');
  }
  if (lineEnding_ == LineEnding::LF || lineEnding_ == LineEnding::CRLF) {
    rs485_.write('\n');
  }
}

const char *Terminal::lineEndingName() const {
  switch (lineEnding_) {
    case LineEnding::None:
      return "none";
    case LineEnding::CR:
      return "cr";
    case LineEnding::LF:
      return "lf";
    case LineEnding::CRLF:
      return "crlf";
  }
  return "unknown";
}
