#pragma once
#include <Arduino.h>
namespace usb_output {
// Application logs are whole lines. Reserve USB output while a large host
// report drains, so those lines cannot be inserted into its middle.
static bool reportPending=false;
inline int availableForWrite() {
 return reportPending ? 0 : Serial.availableForWrite();
}
}
