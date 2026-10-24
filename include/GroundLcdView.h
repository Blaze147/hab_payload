#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace ground_lcd {
struct Telemetry {
 uint8_t profile=255;
 bool signal=false,position=false;
 float rssi=0,snr=0;
 int32_t latE6=0,lonE6=0;
 uint32_t positionAt=0;
};
inline void formatTop(const Telemetry& sample,char* output) {
 char text[40];
 const char profile=sample.profile<5 ? char('0'+sample.profile) : '?';
 if(sample.signal && isfinite(sample.rssi) && isfinite(sample.snr) &&
    sample.rssi>=-999 && sample.rssi<=999 && sample.snr>=-99 && sample.snr<=99) {
  const long snr=lroundf(sample.snr*10);
  const unsigned long magnitude=snr<0 ? -snr : snr;
  char value[16];
  snprintf(value,sizeof(value),"%s%lu.%lu",snr<0?"-":"",magnitude/10,magnitude%10);
  snprintf(text,sizeof(text),"P%c R %ld S %s",profile,lroundf(sample.rssi),value);
  if(strlen(text)>16) snprintf(text,sizeof(text),"P%c R%ld S%s",profile,lroundf(sample.rssi),value);
 } else snprintf(text,sizeof(text),"P%c R -- S --",profile);
 memset(output,' ',16);memcpy(output,text,strlen(text)<16?strlen(text):16);output[16]=0;
}
inline void coordinate(int32_t microdegrees,char* output,size_t capacity) {
 const uint32_t magnitude=microdegrees<0 ? uint32_t(-int64_t(microdegrees)) : uint32_t(microdegrees);
 const uint32_t milli=(magnitude+500)/1000;
 snprintf(output,capacity,"%s%lu.%03lu",microdegrees<0?"-":"",(unsigned long)(milli/1000),(unsigned long)(milli%1000));
}
class CoordinateScroll {
 char message[80]={0};
 size_t offset=0,length=0;
public:
 void frame(const Telemetry& sample,uint32_t now,char* output) {
  // Freeze coordinates AND age until every character has completed this cycle.
  if(!offset) {
   if(sample.position) {
    char lat[20],lon[20];coordinate(sample.latE6,lat,sizeof(lat));coordinate(sample.lonE6,lon,sizeof(lon));
    snprintf(message,sizeof(message),"%s,%s -%luS    ",lat,lon,(unsigned long)(uint32_t(now-sample.positionAt)/1000));
   } else snprintf(message,sizeof(message),"Waiting for GPS    ");
   length=strlen(message);
  }
  for(size_t i=0;i<16;++i) output[i]=message[(offset+i)%length];
  output[16]=0;offset=(offset+1)%length;
 }
};
}
