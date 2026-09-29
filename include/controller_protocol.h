#pragma once

#include <Arduino.h>

namespace ControllerProtocol {

struct CommandDefinition {
  char code;
  const char *name;
  const char *description;
};

constexpr size_t kMaxCommandLength = 74;
constexpr size_t kMaxFrameLength = 96;

extern const CommandDefinition kCommands[];
extern const size_t kCommandCount;

bool isControllerCommand(const char *command);
bool buildFrame(uint8_t address, const char *command, char *frame,
                size_t frameCapacity);
bool parseResponseFrame(const uint8_t *frame, size_t frameLength,
                        uint8_t expectedAddress, const uint8_t *&payload,
                        size_t &payloadLength);

}  // namespace ControllerProtocol
