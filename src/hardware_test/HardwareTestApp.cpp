#include <Arduino.h>
#include <RadioManager.h>
#include "PhysicalRadioAdapter.h"
#ifndef TEST_INTERVAL_MS
#define TEST_INTERVAL_MS 5000UL
#endif
#ifndef TEST_PACKET_LIMIT
#define TEST_PACKET_LIMIT 0UL
#endif
#ifndef TEST_PROFILE
#define TEST_PROFILE 0
#endif
namespace hardware_test {
using namespace radio;
PhysicalRadioAdapter hardware;
RadioManager manager(hardware);
uint8_t buffer[MaxPayload];
uint32_t counter=0, nextAction=0, recoveryAt=0;
bool txObserved=false, waiting=false, replyPending=false;
Config profile() {
 Config c;
#if TEST_PROFILE == 1
 c.spreadingFactor=9; c.codingRate=5;
#elif TEST_PROFILE != 0
#error Unsupported TEST_PROFILE
#endif
 return c;
}
bool due(uint32_t now,uint32_t at) { return int32_t(now-at)>=0; }
void report(const char* operation,Status s) {
 Serial.print(operation); Serial.print(": "); Serial.println(statusName(s));
}
uint32_t packetCounter(const uint8_t* p) {
 uint32_t value=0; for(uint8_t i=0;i<4;++i) value|=uint32_t(p[i])<<(8*i); return value;
}
void makePacket(uint32_t value) {
 for(uint8_t i=0;i<4;++i) buffer[i]=uint8_t(value>>(8*i));
 buffer[4]=215; buffer[5]=0; // Signed temperature in tenths C, little endian.
}
void initialize(uint32_t now) {
 Status s=manager.initialize(profile()); report("Initialization",s);
 recoveryAt=now+5000;
 if(s!=Status::Ok) return;
 Serial.print("Profile P"); Serial.println(TEST_PROFILE);
#ifdef RADIO_RECEIVER
 report("Receive",manager.receive());
#endif
 waiting=false; replyPending=false; txObserved=false; nextAction=now;
}
void setup() { Serial.begin(115200); initialize(millis()); }
void startPacket(uint32_t value,uint32_t now) {
 makePacket(value); Status s=manager.transmit(buffer,6,now); report("TX request",s);
 if(s==Status::Ok) { Serial.print("Counter: "); Serial.println(value); txObserved=true; }
}
void loop() {
 uint32_t now=millis();
 if(manager.state()==State::Fault || manager.state()==State::Uninitialized) {
  if(due(now,recoveryAt)) initialize(now);
  return;
 }
 Status s=manager.poll(now);
 if(s!=Status::Ok) { report("Poll",s); recoveryAt=now+5000; return; }
 if(txObserved && manager.transmission()==TxStatus::Complete) {
  txObserved=false; Serial.println("TX complete (remote delivery unknown)");
#if defined(RADIO_TRANSMITTER) && defined(BIDIRECTIONAL)
  report("Receive",manager.receive()); waiting=true; nextAction=now+30000;
#elif defined(RADIO_RECEIVER)
  report("Receive",manager.receive());
#else
  nextAction=now+TEST_INTERVAL_MS;
#endif
 }
 if(manager.state()==State::Receiving) {
  PacketInfo info; s=manager.read(buffer,sizeof(buffer),info);
  if(s==Status::Ok) {
   Serial.print("RX bytes: "); for(size_t i=0;i<info.length;++i) { if(buffer[i]<16) Serial.print('0'); Serial.print(buffer[i],HEX); Serial.print(' '); }
   Serial.print(" length="); Serial.print(info.length); Serial.print(" RSSI="); Serial.print(info.rssi); Serial.print(" SNR="); Serial.println(info.snr);
   if(info.length==6) { Serial.print("Counter: "); Serial.print(packetCounter(buffer)); Serial.println(" temperature=21.5 C"); }
#ifdef BIDIRECTIONAL
#ifdef RADIO_RECEIVER
   if(info.length==6) { counter=packetCounter(buffer); replyPending=true; nextAction=now+1000; }
#else
   waiting=false; report("Idle",manager.idle()); nextAction=now+TEST_INTERVAL_MS;
#endif
#endif
  } else if(s!=Status::NoPacket) report("RX",s);
 }
#ifdef RADIO_TRANSMITTER
 if(waiting && due(now,nextAction)) { Serial.println("Response timeout"); waiting=false; report("Idle",manager.idle()); nextAction=now+TEST_INTERVAL_MS; }
 if(!waiting && manager.state()==State::Idle && due(now,nextAction) && (TEST_PACKET_LIMIT==0 || counter<TEST_PACKET_LIMIT)) {
  startPacket(counter,now); if(txObserved) ++counter;
 }
#else
#ifdef BIDIRECTIONAL
 if(replyPending && due(now,nextAction) && manager.state()==State::Receiving) { replyPending=false; startPacket(counter,now); }
#endif
#endif
}

} // namespace hardware_test
