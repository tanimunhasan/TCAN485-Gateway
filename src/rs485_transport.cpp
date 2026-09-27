#include "rs485_transport.h"

namespace {

// LilyGo T-CAN485 pin assignments.
constexpr int kRs485TxPin = 22;
constexpr int kRs485RxPin = 21;
constexpr int kRs485CallbackPin = 17;
constexpr int kRs485EnablePin = 19;
constexpr int kBoost5vEnablePin = 16;

}  // namespace

void Rs485Transport::begin(uint32_t baud) {
  pinMode(kBoost5vEnablePin, OUTPUT);
  digitalWrite(kBoost5vEnablePin, HIGH);

  pinMode(kRs485EnablePin, OUTPUT);
  digitalWrite(kRs485EnablePin, HIGH);

  pinMode(kRs485CallbackPin, OUTPUT);
  digitalWrite(kRs485CallbackPin, HIGH);  // Disable local transmit echo.

  serial_.setRxBufferSize(4096);
  setBaud(baud);
}

bool Rs485Transport::setBaud(uint32_t baud) {
  if (baud < 300 || baud > 3000000) {
    return false;
  }

  serial_.end();
  baud_ = baud;
  serial_.begin(baud_, SERIAL_8N1, kRs485RxPin, kRs485TxPin);
  return true;
}

size_t Rs485Transport::write(const uint8_t *data, size_t length) {
  return serial_.write(data, length);
}

size_t Rs485Transport::write(uint8_t value) {
  return serial_.write(value);
}

void Rs485Transport::flush() {
  serial_.flush();
}

int Rs485Transport::available() {
  return serial_.available();
}

int Rs485Transport::read() {
  return serial_.read();
}

uint32_t Rs485Transport::baud() const {
  return baud_;
}
