#pragma once
#include "UsbOutput.h"
#include <Arduino.h>
#include <pico/mutex.h>
#include <math.h>
#include <stdio.h>
#include "BalloonI2c.h"
#include "sensors/Bmp280Compensation.h"

namespace pressure_monitor {
constexpr uint32_t SampleMs=1000, RetryMs=5000;
static bmp280::Calibration calibration;
static bool initialized=false;
static uint8_t address=0,chipId=0;
static uint32_t nextReadAt=0;
auto_init_mutex(readingMutex);
struct Reading {
 float pa=NAN,chipC=NAN;
 const char* status="STARTING";
 bool valid=false;
 uint8_t address=0,id=0;
 uint32_t sequence=0,sampledAt=0;
};
static Reading latest;
// Call only with balloon_i2c::Lock held; all transfers check their result.
inline bool read(uint8_t device,uint8_t reg,uint8_t* bytes,uint8_t count) {
 Wire1.beginTransmission(device);Wire1.write(reg);
 if(Wire1.endTransmission(false)!=0)return false;
 if(Wire1.requestFrom(device,count)!=count)return false;
 for(unsigned i=0;i<count;++i)bytes[i]=uint8_t(Wire1.read());
 return true;
}
inline bool write(uint8_t reg,uint8_t value) {
 Wire1.beginTransmission(address);Wire1.write(reg);Wire1.write(value);
 return Wire1.endTransmission()==0;
}
inline const char* initialize() {
 address=0;chipId=0;
 for(uint8_t candidate=0x76;candidate<=0x77;++candidate) {
  uint8_t id=0;
  if(read(candidate,0xd0,&id,1) && id==0x58) {address=candidate;chipId=id;break;}
 }
 if(!address)return "NO_DEVICE";
 uint8_t status=0,coefficients[24];
 if(!read(address,0xf3,&status,1))return "I2C_ERROR";
 if(status&1)return "CALIBRATING";
 if(!read(address,0x88,coefficients,sizeof(coefficients)) ||
    !bmp280::calibration(coefficients,sizeof(coefficients),calibration))return "CALIBRATION_ERROR";
 // Sleep before changing settings. Normal mode, temperature x2, pressure x16,
 // 500 ms standby, IIR x4. Conversions run in hardware; never wait on the bus.
 if(!write(0xf4,0) || !write(0xf5,0x88) || !write(0xf4,0x57))return "CONFIG_ERROR";
 uint8_t control=0,config=0;
 if(!read(address,0xf4,&control,1) || !read(address,0xf5,&config,1) ||
    control!=0x57 || config!=0x88)return "CONFIG_ERROR";
 initialized=true;return "STARTING";
}
inline void beginSensor() { balloon_i2c::begin();nextReadAt=millis(); }
inline void readSensor() {
 const uint32_t now=millis();
 if(int32_t(now-nextReadAt)<0)return;
 Reading next;next.sampledAt=now;
 {
  balloon_i2c::Lock lock;
  if(!initialized) {
   next.status=initialize();nextReadAt=millis()+(initialized?100:RetryMs);
  } else {
   nextReadAt=now+SampleMs;
   uint8_t id=0,raw[6];
   if(!read(address,0xd0,&id,1) || id!=0x58 || !read(address,0xf7,raw,sizeof(raw))) {
    next.status="I2C_ERROR";initialized=false;
   } else {
    double c=0,pa=0;
    if(!bmp280::compensate(calibration,bmp280::raw20(raw+3),bmp280::raw20(raw),c,pa)) {
     next.status="READ_ERROR";initialized=false;
    } else {
     next.chipC=float(c);next.pa=float(pa);
     // Manufacturer's operating envelope; never label out-of-range data valid.
     next.valid=pa>=30000 && pa<=110000 && c>=-40 && c<=85;
     next.status=next.valid?"OK":"OUT_OF_RANGE";
    }
   }
   if(!initialized)nextReadAt=millis()+RetryMs;
  }
 }
 next.address=address;next.id=chipId;
 mutex_enter_blocking(&readingMutex);
 next.sequence=latest.sequence+1;latest=next;
 mutex_exit(&readingMutex);
}
inline void poll() {
 static uint32_t reported=0;
 Reading reading;
 if(!mutex_try_enter(&readingMutex,nullptr))return;
 reading=latest;mutex_exit(&readingMutex);
 if(!reading.sequence || reading.sequence==reported || !Serial)return;
 char pa[24]="NA",hpa[24]="NA",c[24]="NA",line[224];
 if(isfinite(reading.pa)) {dtostrf(reading.pa,0,1,pa);dtostrf(reading.pa/100.0f,0,2,hpa);}
 if(isfinite(reading.chipC))dtostrf(reading.chipC,0,2,c);
 const int length=snprintf(line,sizeof(line),
  "BARO %s pressure_pa=%s pressure_hpa=%s chip_c=%s sensor=BMP280 address=0x%02X chip_id=0x%02X sda=2 scl=3 sequence=%lu\r\n",
  reading.status,pa,hpa,c,unsigned(reading.address),unsigned(reading.id),(unsigned long)reading.sequence);
 if(length>0 && size_t(length)<sizeof(line) && usb_output::availableForWrite()>=length) {
  Serial.write(reinterpret_cast<const uint8_t*>(line),size_t(length));reported=reading.sequence;
 }
}
}
