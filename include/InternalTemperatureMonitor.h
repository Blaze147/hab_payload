#pragma once
#include "UsbOutput.h"
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <pico/mutex.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace internal_temperature_monitor {
constexpr uint8_t DataPin=26;
constexpr uint32_t ConversionMs=800, SampleIntervalMs=1000, RediscoverMs=5000;
static OneWire bus;
static DallasTemperature sensor(&bus);
static DeviceAddress address={};
static bool found=false, converting=false;
static uint8_t deviceCount=0;
static bool idleHigh=false,presence=false;
static uint32_t requestedAt=0, nextRequestAt=0, nextDiscoveryAt=0;
auto_init_mutex(readingMutex);
struct Reading {
 float c=NAN;
 const char* status="STARTING";
 bool valid=false;
 uint8_t devices=0, rom[8]={};
 bool idleHigh=false,presence=false;
 uint32_t sequence=0, sampledAt=0;
};
static Reading latest;

inline void publish(float c,const char* status,uint32_t now) {
 Reading next;next.c=c;next.status=status;next.valid=strcmp(status,"OK")==0;
 next.devices=deviceCount;next.sampledAt=now;next.idleHigh=idleHigh;next.presence=presence;
 if(found)memcpy(next.rom,address,sizeof(address));
 mutex_enter_blocking(&readingMutex);
 next.sequence=latest.sequence+1;latest=next;
 mutex_exit(&readingMutex);
}
inline void discover(uint32_t now) {
 idleHigh=digitalRead(DataPin)==HIGH;presence=bus.reset()!=0;
 sensor.begin();sensor.setWaitForConversion(false);sensor.setAutoSaveScratchPad(false);
 deviceCount=sensor.getDeviceCount();unsigned matches=0;
 DeviceAddress candidate;
 for(unsigned i=0;i<deviceCount;++i) {
  if(sensor.getAddress(candidate,i) && candidate[0]==0x28 && OneWire::crc8(candidate,7)==candidate[7]) {
   memcpy(address,candidate,sizeof(address));++matches;
  }
 }
 found=matches==1;converting=false;
 nextDiscoveryAt=millis()+RediscoverMs;nextRequestAt=millis();
 if(found) {
  if(!sensor.setResolution(address,12) || sensor.getResolution(address)!=12) {
   publish(NAN,"CONFIG_ERROR",millis());found=false;
  } // Scratchpad setting; EEPROM auto-save is disabled.
 } else publish(NAN,matches>1?"MULTIPLE_DS18B20":deviceCount?"NO_DS18B20":"NO_DEVICE",now);
}
inline void beginSensor() {
 // Only core 1 accesses GP26/1-Wire. Sensor VDD/pull-up use 3.3 V.
 bus.begin(DataPin);
 discover(millis());
}
inline void readSensor() {
 uint32_t now=millis();
 if(!found) {
  if(int32_t(now-nextDiscoveryAt)>=0)discover(now);
  return;
 }
 if(!converting) {
  if(int32_t(now-nextRequestAt)<0)return;
  if(!sensor.requestTemperaturesByAddress(address)) {
   publish(NAN,"REQUEST_ERROR",millis());found=false;nextDiscoveryAt=millis()+RediscoverMs;return;
  }
  requestedAt=millis();converting=true;return;
 }
 // Never report a power-on scratchpad before a requested conversion finishes.
 if(uint32_t(now-requestedAt)<ConversionMs)return;
 const float c=sensor.getTempC(address); // DallasTemperature validates scratchpad CRC.
 const bool valid=c!=DEVICE_DISCONNECTED_C && isfinite(c) && c>=-55 && c<=125;
 now=millis();publish(valid?c:NAN,valid?"OK":"READ_ERROR",now);
 converting=false;
 nextRequestAt=requestedAt+SampleIntervalMs;
 if(!valid) { found=false;nextDiscoveryAt=now+RediscoverMs; }
}
inline void poll() {
 static uint32_t reported=0;
 Reading reading;
 if(!mutex_try_enter(&readingMutex,nullptr))return;
 reading=latest;mutex_exit(&readingMutex);
 if(!reading.sequence || reading.sequence==reported || !Serial)return;
 char c[24]="NA",f[24]="NA",rom[17],line[224];
 if(reading.valid) {dtostrf(reading.c,0,2,c);dtostrf(reading.c*1.8f+32,0,2,f);}
 for(unsigned i=0;i<8;++i)snprintf(rom+2*i,3,"%02X",unsigned(reading.rom[i]));
 const int length=snprintf(line,sizeof(line),
  "INTERNAL_TEMP %s c=%s f=%s sensor=DS18B20 pin=%u resolution_bits=12 devices=%u rom=%s idle_high=%u presence=%u sequence=%lu\r\n",
  reading.status,c,f,unsigned(DataPin),unsigned(reading.devices),rom,unsigned(reading.idleHigh),unsigned(reading.presence),(unsigned long)reading.sequence);
 if(length>0 && size_t(length)<sizeof(line) && usb_output::availableForWrite()>=length) {
  Serial.write(reinterpret_cast<const uint8_t*>(line),size_t(length));reported=reading.sequence;
 }
}
}
