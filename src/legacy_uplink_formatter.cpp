#include "legacy_uplink_formatter.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "clock_service.h"
#include "legacy_uplink_config.h"
#include "wifi_manager.h"

namespace {

constexpr size_t kRs485HeaderLength = 4;
constexpr size_t kRs485TrailerLength = 5;
constexpr size_t kMangaRecordLength = 52;
constexpr uint8_t kMangaMinRecords = 1;
constexpr uint8_t kMangaMaxRecords = 25;

bool isHexDigit(uint8_t value) {
  return std::isxdigit(value) != 0;
}

uint8_t decodeHexByte(uint8_t high, uint8_t low) {
  const auto nibble = [](uint8_t value) {
    if (value >= '0' && value <= '9') {
      return static_cast<uint8_t>(value - '0');
    }
    return static_cast<uint8_t>(
        std::toupper(value) - static_cast<unsigned char>('A') + 10);
  };
  return static_cast<uint8_t>((nibble(high) << 4) | nibble(low));
}

uint8_t checksum(const uint8_t *data, size_t length) {
  uint8_t value = 0;
  while (length-- > 0) {
    value = static_cast<uint8_t>(value + *data++);
  }
  return value;
}

bool nextLine(const uint8_t *data, size_t length, size_t &offset,
              const uint8_t *&line, size_t &lineLength) {
  if (offset >= length) {
    return false;
  }

  line = data + offset;
  for (size_t index = offset; index + 1 < length; ++index) {
    if (data[index] == '\r' && data[index + 1] == '\n') {
      lineLength = index - offset;
      offset = index + 2;
      return true;
    }
  }

  lineLength = length - offset;
  offset = length;
  return true;
}

bool parseMangaCount(const uint8_t *line, size_t length, uint8_t &count) {
  constexpr char kPrefix[] = "L,H,";
  if (length < sizeof(kPrefix) - 1 ||
      memcmp(line, kPrefix, sizeof(kPrefix) - 1) != 0) {
    return false;
  }

  size_t index = sizeof(kPrefix) - 1;
  unsigned value = 0;
  size_t digits = 0;
  while (index < length && std::isdigit(line[index])) {
    value = value * 10 + static_cast<unsigned>(line[index] - '0');
    ++index;
    ++digits;
  }

  if (digits == 0 || index == length || line[index] != ',' ||
      value < kMangaMinRecords || value > kMangaMaxRecords) {
    return false;
  }

  count = static_cast<uint8_t>(value);
  return true;
}

bool hasValidGatewayId() {
  if (strlen(LegacyUplinkConfig::kGatewayId) != 15) {
    return false;
  }
  for (const char *value = LegacyUplinkConfig::kGatewayId; *value != '\0';
       ++value) {
    if (!std::isdigit(static_cast<unsigned char>(*value))) {
      return false;
    }
  }
  return true;
}

}  // namespace

LegacyUplinkFormatter::LegacyUplinkFormatter(const WifiManager &wifi,
                                             const ClockService &clock)
    : wifi_(wifi), clock_(clock) {}

void LegacyUplinkFormatter::begin() {
  preferences_.begin("legacy-uplink", false);
}

UplinkFormatResult LegacyUplinkFormatter::format(
    const uint8_t *rawFrame, size_t rawLength, char *output,
    size_t outputCapacity, size_t &outputLength, const char *&detail) {
  outputLength = 0;
  detail = "";

  if (!LegacyUplinkConfig::kEnabled) {
    detail = "legacy uplink is disabled";
    return UplinkFormatResult::Disabled;
  }
  if (!clock_.hasValidTime()) {
    detail = "UTC time is not synchronized";
    return UplinkFormatResult::TimeNotReady;
  }
  if (rawLength < kRs485HeaderLength + kRs485TrailerLength ||
      rawFrame[0] != '@' || rawFrame[3] != '#' ||
      !isHexDigit(rawFrame[1]) || !isHexDigit(rawFrame[2])) {
    detail = "invalid RS485 frame header";
    return UplinkFormatResult::Rejected;
  }

  const size_t checksumIndex = rawLength - kRs485TrailerLength;
  if (rawFrame[checksumIndex] != '*' ||
      !isHexDigit(rawFrame[checksumIndex + 1]) ||
      !isHexDigit(rawFrame[checksumIndex + 2]) ||
      rawFrame[checksumIndex + 3] != '\r' ||
      rawFrame[checksumIndex + 4] != '\n') {
    detail = "invalid RS485 frame trailer";
    return UplinkFormatResult::Rejected;
  }

  const uint8_t receivedChecksum =
      decodeHexByte(rawFrame[checksumIndex + 1], rawFrame[checksumIndex + 2]);
  const uint8_t calculatedChecksum =
      checksum(rawFrame + kRs485HeaderLength,
               checksumIndex - kRs485HeaderLength);
  if (receivedChecksum != calculatedChecksum) {
    detail = "RS485 response checksum is invalid";
    return UplinkFormatResult::Rejected;
  }

  const uint8_t *payload = rawFrame + kRs485HeaderLength;
  const size_t payloadLength = checksumIndex - kRs485HeaderLength;
  size_t offset = 0;
  const uint8_t *headerLine = nullptr;
  size_t headerLength = 0;
  uint8_t recordCount = 0;
  if (!nextLine(payload, payloadLength, offset, headerLine, headerLength) ||
      !parseMangaCount(headerLine, headerLength, recordCount)) {
    detail = "not a Manga L,H log-page response";
    return UplinkFormatResult::Ignored;
  }

  const uint8_t *columnLine = nullptr;
  size_t columnLength = 0;
  if (!nextLine(payload, payloadLength, offset, columnLine, columnLength) ||
      columnLength == 0) {
    detail = "Manga column-name line is missing";
    return UplinkFormatResult::Rejected;
  }

  if (!hasValidGatewayId() ||
      strlen(LegacyUplinkConfig::kCompatibilityId) == 0 ||
      strlen(LegacyUplinkConfig::kCompatibilityId) > 20) {
    detail = "legacy gateway identity configuration is invalid";
    return UplinkFormatResult::Rejected;
  }

  const uint16_t messageIndex = nextMessageIndex();
  const int headerLengthWritten = snprintf(
      output, outputCapacity, "%s,%04u,08,%s,%ld C0%08lX%02X",
      LegacyUplinkConfig::kGatewayId, messageIndex,
      LegacyUplinkConfig::kCompatibilityId,
      static_cast<long>(wifi_.rssi()),
      static_cast<unsigned long>(clock_.epoch()), recordCount);
  if (headerLengthWritten <= 0 ||
      static_cast<size_t>(headerLengthWritten) >= outputCapacity) {
    detail = "legacy uplink buffer is too small";
    return UplinkFormatResult::Rejected;
  }

  outputLength = static_cast<size_t>(headerLengthWritten);
  for (uint8_t recordIndex = 0; recordIndex < recordCount; ++recordIndex) {
    const uint8_t *record = nullptr;
    size_t recordLength = 0;
    if (!nextLine(payload, payloadLength, offset, record, recordLength) ||
        recordLength != kMangaRecordLength) {
      detail = "Manga record count or record length is invalid";
      return UplinkFormatResult::Rejected;
    }
    if (outputLength + recordLength >= outputCapacity) {
      detail = "legacy uplink exceeds output buffer";
      return UplinkFormatResult::Rejected;
    }
    for (size_t character = 0; character < recordLength; ++character) {
      if (!isHexDigit(record[character])) {
        detail = "Manga record contains non-hex data";
        return UplinkFormatResult::Rejected;
      }
      output[outputLength++] = static_cast<char>(
          std::toupper(static_cast<unsigned char>(record[character])));
    }
  }

  while (offset + 1 < payloadLength && payload[offset] == '\r' &&
         payload[offset + 1] == '\n') {
    offset += 2;
  }
  if (offset != payloadLength) {
    detail = "Manga response contains unexpected trailing data";
    return UplinkFormatResult::Rejected;
  }

  output[outputLength] = '\0';
  detail = "Manga response formatted";
  return UplinkFormatResult::Formatted;
}

bool LegacyUplinkFormatter::forwardRawDiagnostics() const {
  return LegacyUplinkConfig::kForwardRawDiagnostics;
}

uint16_t LegacyUplinkFormatter::nextMessageIndex() {
  const uint16_t current = preferences_.getUShort("message-index", 0) % 10000;
  preferences_.putUShort("message-index",
                         static_cast<uint16_t>((current + 1) % 10000));
  return current;
}
