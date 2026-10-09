#include <Arduino.h>

#ifdef HARDWARE_TEST
#include "HardwareTestApp.h"
void setup() { hardware_test::setup(); }
void loop() { hardware_test::loop(); }
#else
#include <RadioManager.h>
#include "PhysicalRadioAdapter.h"
namespace {
PhysicalRadioAdapter hardware;
radio::RadioManager manager(hardware);
}
// Stage 1 foundation: initialize and stay idle until a future application uses it.
void setup() {
 Serial.begin(115200);
 Serial.print("Initialization: ");
 Serial.println(radio::statusName(manager.initialize(radio::Config{})));
}
void loop() { manager.poll(millis()); }
#endif
