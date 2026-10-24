#pragma once
#include "UsbOutput.h"
#include <MessageManager.h>
#include "TelemetrySource.h"
#include "GpsMonitor.h"
#if !defined(BENCH_GPS_ONLY) && !defined(BENCH_GPS_PAYLOAD)
#include "EnvironmentMonitor.h"
#include "RtdMonitor.h"
#include "InternalTemperatureMonitor.h"
#include "PressureMonitor.h"
#endif

namespace telemetry {
inline void poll(MessageManager& messages,uint32_t now) {
 if(!messages.ready(now))return;
 messaging::SensorData data;data.uptimeMs=now;
#if !defined(BENCH_GPS_ONLY) && !defined(BENCH_GPS_PAYLOAD)
 environment_monitor::Reading env;
 if(!mutex_try_enter(&environment_monitor::readingMutex,nullptr))return;
 env=environment_monitor::latest;
 mutex_exit(&environment_monitor::readingMutex);
 rtd_monitor::Reading rtd;
 if(!mutex_try_enter(&rtd_monitor::readingMutex,nullptr))return;
 rtd=rtd_monitor::latest;
 mutex_exit(&rtd_monitor::readingMutex);
 internal_temperature_monitor::Reading internal;
 if(!mutex_try_enter(&internal_temperature_monitor::readingMutex,nullptr))return;
 internal=internal_temperature_monitor::latest;
 mutex_exit(&internal_temperature_monitor::readingMutex);
 pressure_monitor::Reading pressure;
 if(!mutex_try_enter(&pressure_monitor::readingMutex,nullptr))return;
 pressure=pressure_monitor::latest;
 mutex_exit(&pressure_monitor::readingMutex);
 now=millis(); // Core 1 may have published a sample after this poll began.
 data.uptimeMs=now;
 const bool valid=env.valid && env.sequence && uint32_t(now-env.sampledAt)<6000;
 data.internalTemperature=data.internalHumidity=valid;
 if(valid) {
  data.internalTemperatureCentiC=int16_t(lround(env.c*100));
  data.internalHumidityCentiPct=uint16_t(lround(env.humidity*100));
 }
 data.externalRtd=rtd.valid && rtd.sequence && uint32_t(now-rtd.sampledAt)<6000 && rtd.c>=-200 && rtd.c<=327.67f;
 if(data.externalRtd)data.externalRtdCentiC=int16_t(lround(rtd.c*100));
 data.internalLowTemperature=internal.valid && internal.sequence && uint32_t(now-internal.sampledAt)<6000;
 if(data.internalLowTemperature)data.internalLowTemperatureCentiC=int16_t(lround(internal.c*100));
 data.pressure=pressure.valid && pressure.sequence && uint32_t(now-pressure.sampledAt)<6000;
 if(data.pressure)data.pressurePa=uint32_t(lround(pressure.pa));
#endif
 auto& gps=gps_monitor::gps;
 const bool stream=gps_monitor::received && uint32_t(now-gps_monitor::lastByteMs)<3000;
 const bool fix=stream && gps.location.isValid() && gps.location.age()<3000;
 const bool altitude=stream && gps.altitude.isValid() && gps.altitude.age()<3000;
 const bool satellites=stream && gps.satellites.isValid() && gps.satellites.age()<3000;
 data.gps=true;data.gpsFlags=(stream?1:0)|(fix?2:0)|(altitude?4:0)|(satellites?8:0);
 if(fix) { data.latitudeE6=int32_t(lround(gps.location.lat()*1e6));data.longitudeE6=int32_t(lround(gps.location.lng()*1e6)); }
 if(altitude)data.altitudeDm=int32_t(lround(gps.altitude.meters()*10));
 if(satellites)data.satellites=uint16_t(gps.satellites.value());
 messages.setInterPacketPause(pauseMs(messages.radioConfig()));
#ifdef BENCH_PADDED_TELEMETRY
 // Opaque transport test: real GPS envelope plus padding. Leading ASCII keeps
 // ground software from interpreting the padding as genuine sensor readings.
 uint8_t bytes[44]={'B','E','N','C','H'};
 uint8_t gpsBytes[messaging::MaxSensorBytes];
 const size_t gpsLength=messaging::encodeSensors(data,gpsBytes,sizeof(gpsBytes));
 if(!gpsLength || gpsLength>sizeof(bytes)-5)return;
 memcpy(bytes+5,gpsBytes,gpsLength);
 for(size_t i=5+gpsLength;i<sizeof(bytes);++i)bytes[i]=uint8_t(i);
 messaging::MessageView view;view.data=bytes;view.length=sizeof(bytes);
 const auto result=messages.send(view,now);
#else
 const auto result=messages.sendSensors(data,now);
#endif
 if(Serial && result==protocol::Result::Ok) {
  char line[256];
  const unsigned mask=(data.internalTemperature?1:0)|(data.internalHumidity?2:0)|
   (data.internalLowTemperature?4:0)|(data.externalRtd?8:0)|(data.pressure?16:0)|(data.gps?32:0);
  const int length=snprintf(line,sizeof(line),
   "TELEMETRY QUEUED schema=A1 records=0x%02X dht_centi_c=%d rh_centi_pct=%u internal_centi_c=%d external_centi_c=%d pressure_pa=%lu gps_flags=0x%02X packet_result=%u delivery=unknown\r\n",
   mask,int(data.internalTemperatureCentiC),unsigned(data.internalHumidityCentiPct),
   int(data.internalLowTemperatureCentiC),int(data.externalRtdCentiC),
   (unsigned long)data.pressurePa,unsigned(data.gpsFlags),unsigned(messages.lastSendResult()));
  if(length>0 && size_t(length)<sizeof(line) && usb_output::availableForWrite()>=length)
   Serial.write(reinterpret_cast<const uint8_t*>(line),size_t(length));
 }
}
}
