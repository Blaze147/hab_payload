#pragma once
#include "UsbOutput.h"

#include <Arduino.h>
#include <TinyGPS++.h>
#include "BalloonI2c.h"
#include <stdio.h>

// Application-only USB monitor; no dependency on the radio/link managers.
namespace gps_monitor {
constexpr uint8_t Address = 0x42;
constexpr uint32_t PollIntervalMs = 50;
static TinyGPSPlus gps;
static uint32_t reportAt = 0, lastByteMs = 0, pollAt = 0, i2cErrors = 0;
static bool received = false;

inline void begin() {
 balloon_i2c::begin();
}

inline void readStream(uint32_t now) {
 if(uint32_t(now-pollAt) < PollIntervalMs) return;
 pollAt = now;
 balloon_i2c::Lock lock;
 // u-blox DDC: 0xFD/0xFE are the available-byte count; the pointer then
 // advances to 0xFF, which streams NMEA without changing receiver settings.
 Wire1.beginTransmission(Address);
 Wire1.write(0xfd);
 if(Wire1.endTransmission(false) != 0) { ++i2cErrors; return; }
 if(Wire1.requestFrom(Address, uint8_t(2)) != 2) { ++i2cErrors; return; }
 unsigned available = unsigned(Wire1.read()) << 8;
 available |= unsigned(Wire1.read());
 if(available == 0xffff) return;
 // Bound bus work so a backlog cannot monopolize radio/link servicing.
 if(available > 256) available = 256;
 while(available) {
  const uint8_t chunk = available > 32 ? 32 : uint8_t(available);
  const unsigned count = Wire1.requestFrom(Address, chunk);
  while(Wire1.available()) {
   gps.encode(char(Wire1.read()));
   received = true;
   lastByteMs = millis();
  }
  if(count != chunk) { ++i2cErrors; return; }
  available -= count;
 }
}

inline void poll(uint32_t now) {
 readStream(now);
 now = millis(); // I²C reads can advance the timestamp used for freshness.
 if(uint32_t(now - reportAt) < 1000) return;
 reportAt = now;
 if(!Serial) return;

 const bool stream = received && uint32_t(now-lastByteMs) < 3000;
 const bool fix = stream && gps.location.isValid() && gps.location.age() < 3000;
 const char* status = !received || uint32_t(now-lastByteMs) >= 3000 ? "NO_DATA" :
                      fix ? "FIX" : "NO_FIX";
 char position[72] = "lat=NA lon=NA";
 if(fix) {
  char latitude[24], longitude[24];
  dtostrf(gps.location.lat(), 0, 6, latitude);
  dtostrf(gps.location.lng(), 0, 6, longitude);
  snprintf(position, sizeof(position), "lat=%s lon=%s", latitude, longitude);
 }
 char altitude[24] = "NA", speed[24] = "NA", utc[32] = "NA";
 if(stream && gps.altitude.isValid() && gps.altitude.age() < 3000)
  dtostrf(gps.altitude.meters(), 0, 1, altitude);
 if(stream && gps.speed.isValid() && gps.speed.age() < 3000)
  dtostrf(gps.speed.kmph(), 0, 1, speed);
 if(stream && gps.date.isValid() && gps.time.isValid() && gps.date.age() < 3000 && gps.time.age() < 3000)
  snprintf(utc, sizeof(utc), "%04u-%02u-%02uT%02u:%02u:%02uZ",
           unsigned(gps.date.year()), unsigned(gps.date.month()), unsigned(gps.date.day()),
           unsigned(gps.time.hour()), unsigned(gps.time.minute()), unsigned(gps.time.second()));
 char line[384];
 const int length = snprintf(line, sizeof(line),
  "GPS %s %s alt_m=%s speed_kmh=%s sats=%ld utc=%s chars=%lu checksum_fail=%lu pps=NA pps_age_ms=NA sensor=MAX-M10S i2c_errors=%lu\r\n",
  status, position, altitude, speed,
  stream && gps.satellites.isValid() && gps.satellites.age() < 3000 ? long(gps.satellites.value()) : -1L,
  utc, gps.charsProcessed(), gps.failedChecksum(), (unsigned long)i2cErrors);
 // Drop a report under USB backpressure instead of waiting for a host.
 if(length > 0 && size_t(length) < sizeof(line) && usb_output::availableForWrite() >= length)
  Serial.write(reinterpret_cast<const uint8_t*>(line), size_t(length));
}
}
