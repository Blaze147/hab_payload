#include <Arduino.h>
#include <RadioServer.h>
#include "PhysicalRadioAdapter.h"
#include "BoardSerial.h"
#ifdef SPLIT_DIAGNOSTICS
extern "C" char __heap_start;
#endif
namespace {
#ifdef SPLIT_DIAGNOSTICS
uint8_t* paintedEnd=nullptr;
void paintStack() {
 paintedEnd=reinterpret_cast<uint8_t*>(SP)-32;
 for(uint8_t* p=reinterpret_cast<uint8_t*>(&__heap_start);p<paintedEnd;++p) *p=0xa5;
}
unsigned minimumStackFree() {
 uint8_t* p=reinterpret_cast<uint8_t*>(&__heap_start);
 while(p<paintedEnd && *p==0xa5) ++p;
 return unsigned(p-reinterpret_cast<uint8_t*>(&__heap_start));
}
#endif
PhysicalRadioAdapter hardware;
radio::RadioManager radioManager(hardware);
BoardSerial uart;
board_link::RadioServer server(uart,radioManager);
uint32_t reportAt=0;
}
void setup() { Serial.begin(115200);uart.begin();
#ifdef SPLIT_DIAGNOSTICS
 paintStack();
#endif
 server.begin(); }
void loop() {
 server.poll();
 const uint32_t now=millis();
 if(Serial && uint32_t(now-reportAt)>=2000) {
  reportAt=now;Serial.print(F("SPLIT_RADIO uart=100000 commands="));Serial.print(server.commands);
  Serial.print(F(" connected="));Serial.print(server.connected());Serial.print(F(" state="));Serial.print(uint8_t(radioManager.state()));
  Serial.print(F(" bad="));Serial.print(server.badFrames);Serial.print(F(" rejected="));Serial.print(server.rejectedCommands);
#ifdef SPLIT_DIAGNOSTICS
  Serial.print(F(" minStackFree="));Serial.print(minimumStackFree());
#endif
  Serial.println();
 }
}
