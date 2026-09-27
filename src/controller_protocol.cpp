#include "controller_protocol.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace ControllerProtocol {

const CommandDefinition kCommands[] = {
    {'O', "unlock", "Unlock or lock the controller CLI"},
    {'A', "address", "Get or set RS485 device address (1-255)"},
    {'B', "battery", "Get or set battery/external-supply low threshold"},
    {'F', "finish", "Finish configuration and select run mode"},
    {'G', "watchdog", "Run watchdog test loop (non-normal modes only)"},
    {'H', "help", "Print controller command summary"},
    {'P', "profile", "Get or set duty-cycle profile"},
    {'Q', "purge", "Get or set user-profile purge duration"},
    {'L', "log", "Fetch a page of N2O result log"},
    {'C', "cursor", "Get, set, or reset N2O log paging cursor"},
    {'S', "saturated", "Get or set N2O saturated raw threshold"},
    {'N', "low", "Get or set N2O low raw threshold"},
    {'I', "minutes", "Get or set minutes of day"},
    {'T', "time", "Get or set UTC Unix timestamp"},
    {'R', "mode", "Get or set operation mode"},
    {'V', "version", "Print version and build information"},
    {'X', "echo", "Debug echo XON/XOFF control"},
    {'Y', "days", "Get or set day/reset count"},
};

const size_t kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);

namespace {

uint8_t checksum(const char *text) {
  uint8_t value = 0;
  while (*text != '\0') {
    value = static_cast<uint8_t>(value + static_cast<uint8_t>(*text++));
  }
  return value;
}

bool isKnownCode(char code) {
  for (size_t index = 0; index < kCommandCount; ++index) {
    if (kCommands[index].code == code) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool isControllerCommand(const char *command) {
  if (command == nullptr) {
    return false;
  }

  const size_t length = strlen(command);
  if (length < 3 || length > kMaxCommandLength || command[1] != '=') {
    return false;
  }

  const char code =
      static_cast<char>(std::toupper(static_cast<unsigned char>(command[0])));
  return isKnownCode(code);
}

bool buildFrame(uint8_t address, const char *command, char *frame,
                size_t frameCapacity) {
  if (!isControllerCommand(command) || frame == nullptr ||
      frameCapacity == 0) {
    return false;
  }

  char normalizedCommand[kMaxCommandLength + 1];
  strncpy(normalizedCommand, command, sizeof(normalizedCommand));
  normalizedCommand[0] = static_cast<char>(
      std::toupper(static_cast<unsigned char>(normalizedCommand[0])));

  const int length = snprintf(frame, frameCapacity, "@%02X#%s*%02X\r\n",
                              address, normalizedCommand,
                              checksum(normalizedCommand));
  return length > 0 && static_cast<size_t>(length) < frameCapacity;
}

}  // namespace ControllerProtocol
