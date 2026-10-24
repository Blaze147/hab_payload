#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include <pico/mutex.h>
#include <stdio.h>
#include <string.h>
#include "GroundLcdView.h"

// SunFounder PCF8574 backpack: P0=RS, P1=RW, P2=E, P3=backlight,
// P4..P7=D4..D7. Core 1 owns Wire1; core 0 only reads status.
namespace ground_lcd {
enum State : uint8_t { Pending, PinsFailed, Missing, Ambiguous, WriteFailed, Scrolling, TestPattern };
static std::atomic<uint8_t> state{Pending};
static std::atomic<uint32_t> frames{0}, errors{0};
static std::atomic<uint32_t> foundMask{0};
static uint8_t address=0;
static uint32_t nextFrame=0;
static CoordinateScroll coordinateScroll;
auto_init_mutex(telemetryMutex);
static Telemetry telemetry;
inline void updateLink(uint8_t profile,bool signal,float rssi,float snr) {
 if(!mutex_try_enter(&telemetryMutex,nullptr)) return;
 telemetry.profile=profile;telemetry.signal=signal;telemetry.rssi=rssi;telemetry.snr=snr;
 mutex_exit(&telemetryMutex);
}
inline void updatePosition(int32_t lat,int32_t lon,uint32_t now) {
 if(!mutex_try_enter(&telemetryMutex,nullptr)) return;
 telemetry.position=true;telemetry.latE6=lat;telemetry.lonE6=lon;telemetry.positionAt=now;
 mutex_exit(&telemetryMutex);
}

inline bool output(uint8_t value) {
 Wire1.beginTransmission(address);
 Wire1.write(uint8_t(value|0x08)); // backlight on; RW always low
 if(Wire1.endTransmission()==0) return true;
 errors.fetch_add(1);return false;
}
inline bool nibble(uint8_t value) {
 if(!output(value) || !output(uint8_t(value|0x04))) return false;
 delayMicroseconds(10);
 if(!output(value)) return false;
 delay(2);return true;
}
inline bool send(uint8_t value,bool data=false) {
 const uint8_t rs=data?1:0;
 return nibble(uint8_t((value&0xf0)|rs)) && nibble(uint8_t((value<<4)|rs));
}
inline bool row(uint8_t line,const char* text) {
 if(!send(uint8_t(line?0xc0:0x80))) return false;
 bool ended=false;
 for(size_t i=0;i<16;++i) {
  char c=ended?' ':text[i];
  if(!c) { ended=true;c=' '; }
  if(c<32 || c>126) c=' ';
  if(!send(uint8_t(c),true)) return false;
 }
 return true;
}
inline void begin() {
 if(!Wire1.setSDA(2) || !Wire1.setSCL(3)) { state.store(PinsFailed);return; }
 Wire1.begin();Wire1.setClock(100000);Wire1.setTimeout(20);
 // Scan only the two PCF8574 address ranges. Do not send LCD data to
 // unrelated devices or guess when multiple backpacks acknowledge.
 uint32_t mask=0;unsigned count=0;
 for(uint8_t i=0;i<16;++i) {
  const uint8_t candidate=uint8_t(i<8?0x20+i:0x38+i-8);
  Wire1.beginTransmission(candidate);
  if(Wire1.endTransmission()==0) { mask|=uint32_t(1)<<i;address=candidate;++count; }
 }
 foundMask.store(mask);
 if(count!=1) { state.store(count?Ambiguous:Missing);return; }
 // Explicitly lower E before resynchronizing, including after an MCU-only
 // reset while the backpack and LCD remain powered. Allow a slow power ramp.
 delay(1000);
 bool ok=output(0);delay(5);
 ok=ok && nibble(0x30);delay(5);
 ok=ok && nibble(0x30);delay(5);
 ok=ok && nibble(0x30);delay(5);
 ok=ok && nibble(0x20);delay(5);
 ok=ok && send(0x28) && send(0x08) && send(0x01);
 delay(5);
 ok=ok && send(0x06) && send(0x0c);
 char initialTop[17];formatTop(Telemetry{},initialTop);
 ok=ok && row(0,initialTop) && row(1,"Waiting for GPS ");
 if(!ok) { state.store(WriteFailed);return; }
 state.store(Scrolling);nextFrame=millis();
}
inline void poll() {
 if(state.load()!=Scrolling || int32_t(millis()-nextFrame)<0) return;
 Telemetry sample;
 if(!mutex_try_enter(&telemetryMutex,nullptr)) return;
 sample=telemetry;mutex_exit(&telemetryMutex);
 char top[17],window[17];
 formatTop(sample,top);coordinateScroll.frame(sample,millis(),window);
 if(!row(0,top) || !row(1,window)) { state.store(WriteFailed);return; }
 // Move the coordinate text leftward; leave the signal row fixed.
 frames.fetch_add(1);
 nextFrame=millis()+350;
}
inline void report() {
 static bool reported=false;static uint32_t lastReport=0;
 const uint8_t current=state.load();
 if(!Serial || current==Pending || (reported && uint32_t(millis()-lastReport)<5000)) return;
 reported=true;lastReport=millis();
 Serial.print("GROUND_LCD I2C1 SDA=GP2 SCL=GP3 hz=100000 addresses=");
 const uint32_t mask=foundMask.load();
 for(uint8_t i=0;i<16;++i) if(mask&(uint32_t(1)<<i)) {
  Serial.print("0x");Serial.print(i<8?0x20+i:0x38+i-8,HEX);Serial.print(' ');
 }
 const char* names[]={"pending","pins_failed","not_found","multiple_devices","write_failed","scrolling","fixed_test_pattern"};
 Serial.print(" state=");Serial.print(names[current]);
 Serial.print(" frames=");Serial.print(frames.load());Serial.print(" errors=");Serial.println(errors.load());
}
}
