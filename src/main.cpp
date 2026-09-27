#include <Arduino.h>

#include "rs485_transport.h"
#include "terminal.h"

namespace {

Rs485Transport rs485;
Terminal terminal(rs485);

}  // namespace

void setup() {
  rs485.begin();
  terminal.begin();
}

void loop() {
  terminal.service();
}
