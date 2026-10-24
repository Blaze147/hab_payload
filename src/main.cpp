#include <Arduino.h>

#if defined(LINK_TEST) && defined(HARDWARE_TEST)
#error Choose either LINK_TEST or HARDWARE_TEST
#endif
#ifdef LINK_TEST
#include "LinkTestApp.h"
void setup() { link_test::setup(); }
void loop() { link_test::loop(); }
#elif defined(HARDWARE_TEST)
#include "HardwareTestApp.h"
void setup() { hardware_test::setup(); }
void loop() { hardware_test::loop(); }
#else
#include <RadioManager.h>
#include <ProtocolManager.h>
#include <MessageManager.h>
#include <LinkManager.h>
#include <StatisticsManager.h>
#include "PhysicalRadioAdapter.h"
namespace {
PhysicalRadioAdapter hardware;
radio::RadioManager manager(hardware);
StatisticsManager statistics;
ProtocolManager packetManager(manager, statistics);
MessageManager messages(packetManager, statistics);
LinkManager link(packetManager, statistics);
}
// Role-aware link runtime. Application message production remains separate.
void setup() {
 statistics.begin(); packetManager.begin(); messages.begin(); link.begin();
 Serial.begin(115200);
 Serial.print("Initialization: ");
 const auto initialized=manager.initialize(radio::Config{});
 Serial.println(radio::statusName(initialized));
 if(initialized==radio::Status::Ok) {
  LinkManager::Config config;
#ifdef RADIO_TRANSMITTER
  link.start(LinkManager::Role::Balloon,config,millis());
#else
  link.start(LinkManager::Role::Ground,config,millis());
#endif
 }
}
void loop() {
 const uint32_t now=millis();
 packetManager.poll(now); messages.poll(now); link.poll(now);
}
#endif
