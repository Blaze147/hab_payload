#pragma once
#include <Wire.h>
#include <pico/mutex.h>
namespace balloon_i2c {
constexpr uint8_t SdaPin = 2, SclPin = 3;
auto_init_mutex(busMutex);
// Both cores may initialize the bus; initialization and transactions serialize.
struct Lock {
 Lock() { mutex_enter_blocking(&busMutex); }
 ~Lock() { mutex_exit(&busMutex); }
};
inline void begin() {
 Lock lock;
 static bool started = false;
 if(started) return;
#ifdef APPLICATION_WATCHDOG
 // A warm reset may interrupt a slave mid-byte. Release the bus before the
 // application starts, so a warm reset does not leave a slave holding SDA.
 pinMode(SdaPin,INPUT_PULLUP);pinMode(SclPin,INPUT_PULLUP);
 for(unsigned i=0;i<9 && !digitalRead(SdaPin);++i) {
  digitalWrite(SclPin,LOW);pinMode(SclPin,OUTPUT);busy_wait_us_32(10);
  pinMode(SclPin,INPUT_PULLUP);
  const uint32_t released=micros();
  while(!digitalRead(SclPin) && uint32_t(micros()-released)<1000) busy_wait_us_32(1);
  busy_wait_us_32(20);
 }
 digitalWrite(SclPin,LOW);pinMode(SclPin,OUTPUT);
 digitalWrite(SdaPin,LOW);pinMode(SdaPin,OUTPUT);busy_wait_us_32(10);
 pinMode(SclPin,INPUT_PULLUP);busy_wait_us_32(1000);
 pinMode(SdaPin,INPUT_PULLUP);busy_wait_us_32(20);
#endif
 Wire1.setSDA(SdaPin); Wire1.setSCL(SclPin); Wire1.begin();
 Wire1.setClock(100000); Wire1.setTimeout(10);
 started = true;
}
}
