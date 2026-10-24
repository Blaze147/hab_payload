#pragma once
#include "UsbOutput.h"

#include <Arduino.h>
#include <DHT.h>
#include <math.h>
#include <pico/mutex.h>
#include <stdio.h>

namespace environment_monitor {
constexpr uint8_t DataPin = 8;
// Raw hardware frames use DHT22's 16-bit tenths encoding.
static DHT sensor(DataPin, DHT22);
auto_init_mutex(readingMutex);
struct Reading {
 float c = NAN, humidity = NAN;
 uint32_t sequence = 0, sampledAt = 0;
 bool valid = false;
};
static Reading latest;

// Only core 1 touches the sensor. Its timing/interrupt lock cannot block the
// core 0 GPS, USB, PPS, and radio UART interrupts.
inline void beginSensor() {
 sensor.begin();
 delay(2000); // Sensor power-up settling, on core 1 only.
}
inline void readSensor() {
 static uint32_t lastRead = millis() - 2100;
 const uint32_t now = millis();
 if(uint32_t(now - lastRead) < 2100) return;
 lastRead = now;
 Reading next;
 next.sampledAt = now;
 next.c = sensor.readTemperature();
 next.humidity = sensor.readHumidity(); // Same cached/checksummed DHT frame.
 next.valid = isfinite(next.c) && isfinite(next.humidity) &&
              next.c >= -40 && next.c <= 80 && next.humidity >= 0 && next.humidity <= 100;
 mutex_enter_blocking(&readingMutex);
 next.sequence = latest.sequence + 1;
 latest = next;
 mutex_exit(&readingMutex);
}
inline void poll() {
 static uint32_t reported = 0;
 Reading reading;
 if(!mutex_try_enter(&readingMutex, nullptr)) return;
 reading = latest;
 mutex_exit(&readingMutex);
 if(reading.sequence == reported || !Serial) return;
 char c[24] = "NA", humidity[24] = "NA", line[160];
 if(reading.valid) {
  dtostrf(reading.c, 0, 1, c);
  dtostrf(reading.humidity, 0, 1, humidity);
 }
 const int length = snprintf(line, sizeof(line),
  "ENV %s c=%s humidity_pct=%s sensor=DHT22 pin=%u sequence=%lu\r\n",
  reading.valid ? "OK" : "READ_ERROR", c, humidity, unsigned(DataPin), (unsigned long)reading.sequence);
 if(length > 0 && size_t(length) < sizeof(line) && usb_output::availableForWrite() >= length) {
  Serial.write(reinterpret_cast<const uint8_t*>(line), size_t(length));
  reported = reading.sequence;
 }
}
}
