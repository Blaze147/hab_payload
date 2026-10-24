#pragma once
#include "UsbOutput.h"
#include <Adafruit_MAX31865.h>
#include <pico/mutex.h>
#include <math.h>
namespace rtd_monitor {
constexpr uint8_t Cs = 13;
constexpr float NominalOhms = 100.0f, ReferenceOhms = 430.0f;
// Three-wire probe operated as two-wire, using one red and the white lead.
// Red-to-red loop measured 0.35 ohm; assumes equal red/white lead resistance
// and meter test-lead resistance already removed. PT100 CVD gives +0.8845
// to +0.8902 C error over -40..-20 C. Fixed correction requested by user.
constexpr float TemperatureOffsetC = -0.89f;
static Adafruit_MAX31865 sensor(Cs, &SPI1);
auto_init_mutex(readingMutex);
struct Reading {
 float c = NAN, uncorrectedC = NAN, ohms = NAN;
 uint16_t raw = 0;
 uint8_t fault = 0;
 bool valid = false;
 uint32_t sequence = 0, sampledAt = 0;
};
static Reading latest;
inline void beginSensor() {
 // Camera initialized SPI1 on GP10/11/12. Only core 1 uses this SPI bus.
 sensor.begin(MAX31865_2WIRE);
 sensor.enable50Hz(false);
}
inline void readSensor() {
 static uint32_t lastRead = millis() - 1000;
 if(uint32_t(millis() - lastRead) < 1000) return;
 lastRead = millis();
 Reading next;
 next.sampledAt = millis();
 next.raw = sensor.readRTD();
 next.fault = sensor.readFault(MAX31865_FAULT_NONE);
 next.ohms = next.raw * ReferenceOhms / 32768.0f;
 next.uncorrectedC = sensor.calculateTemperature(next.raw, NominalOhms, ReferenceOhms);
 next.c = next.uncorrectedC + TemperatureOffsetC;
 next.valid = !next.fault && next.raw > 0 && next.raw < 32767 &&
              isfinite(next.c) && next.c >= -200 && next.c <= 850;
 mutex_enter_blocking(&readingMutex);
 next.sequence = latest.sequence + 1; latest = next;
 mutex_exit(&readingMutex);
}
inline void poll() {
 static uint32_t reported = 0;
 Reading reading;
 if(!mutex_try_enter(&readingMutex, nullptr)) return;
 reading = latest; mutex_exit(&readingMutex);
 if(!reading.sequence || reading.sequence == reported || !Serial) return;
 char c[24] = "NA", f[24] = "NA", uncorrected[24] = "NA", ohms[24], line[256];
 if(reading.valid) {
  dtostrf(reading.uncorrectedC, 0, 2, uncorrected);
  dtostrf(reading.c, 0, 2, c);
  dtostrf(reading.c * 1.8f + 32, 0, 2, f);
 }
 dtostrf(reading.ohms, 0, 3, ohms);
 const int length = snprintf(line, sizeof(line),
  "RTD %s c=%s f=%s resistance_ohm=%s raw=%u fault=0x%02X sensor=MAX31865 wires=2 cs=13 rref_ohm=430 uncorrected_c=%s offset_c=-0.89 sequence=%lu\r\n",
  reading.valid ? "OK" : "READ_ERROR", c, f, ohms,
  unsigned(reading.raw), unsigned(reading.fault), uncorrected, (unsigned long)reading.sequence);
 if(length > 0 && size_t(length) < sizeof(line) && usb_output::availableForWrite() >= length) {
  Serial.write(reinterpret_cast<const uint8_t*>(line), size_t(length)); reported = reading.sequence;
 }
}
}
