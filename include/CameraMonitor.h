#pragma once
#include "UsbOutput.h"
#include <Arduino.h>
#include <SPI.h>
#include "BalloonI2c.h"
#include <pico/mutex.h>
#include <stdio.h>
#include <string.h>
#include "camera/ov2640_registers.h"

namespace camera_monitor {
constexpr uint8_t Cs = 9;
constexpr uint32_t MaxImage = 32768, IntervalMs = 60000;
auto_init_mutex(cameraMutex);
static uint8_t image[MaxImage];
struct Shared {
 const char* status = "STARTING";
 bool spi = false, ready = false;
 uint16_t sensorId = 0;
 uint32_t sequence = 0, length = 0, crc = 0;
};
static Shared shared;
static uint8_t phase = 0;
static uint32_t attemptedAt = 0, phaseAt = 0, fifoLength = 0, readAt = 0;
static bool initialized = false;

inline void select() {
 SPI1.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
 digitalWrite(Cs, LOW);
}
inline void deselect() { digitalWrite(Cs, HIGH); SPI1.endTransaction(); }
inline void writeReg(uint8_t reg, uint8_t value) {
 select(); SPI1.transfer(reg | 0x80); SPI1.transfer(value); deselect();
}
inline uint8_t readReg(uint8_t reg) {
 select(); SPI1.transfer(reg & 0x7f); const uint8_t value = SPI1.transfer(0); deselect(); return value;
}
inline bool sensorWrite(uint8_t reg, uint8_t value) {
 balloon_i2c::Lock lock;
 Wire1.beginTransmission(0x30); Wire1.write(reg); Wire1.write(value);
 return Wire1.endTransmission() == 0;
}
inline int sensorRead(uint8_t reg) {
 balloon_i2c::Lock lock;
 Wire1.beginTransmission(0x30); Wire1.write(reg);
 if(Wire1.endTransmission() != 0 || Wire1.requestFrom(uint8_t(0x30), uint8_t(1)) != 1) return -1;
 return Wire1.read();
}
template<size_t N> inline bool writeTable(const camera_registers::sensor_reg (&table)[N]) {
 for(size_t i = 0; i < N; ++i) {
  if(table[i].reg == 0xff && table[i].val == 0xff) return true;
  if(!sensorWrite(table[i].reg, table[i].val)) return false;
 }
 return false;
}
inline void setStatus(const char* status) {
 mutex_enter_blocking(&cameraMutex); shared.status = status; mutex_exit(&cameraMutex);
}
inline void begin() {
 pinMode(13, OUTPUT); digitalWrite(13, HIGH); // Deselect RTD before any SPI traffic.
 pinMode(Cs, OUTPUT); digitalWrite(Cs, HIGH);
 SPI1.setSCK(10); SPI1.setTX(11); SPI1.setRX(12); SPI1.begin();
 balloon_i2c::begin();
 attemptedAt = millis() - IntervalMs;
}
inline bool initialize() {
 writeReg(0x00, 0x55); const bool first = readReg(0x00) == 0x55;
 writeReg(0x00, 0xaa); const bool spi = first && readReg(0x00) == 0xaa;
 int high = -1, low = -1;
 if(sensorWrite(0xff, 1)) { high = sensorRead(0x0a); low = sensorRead(0x0b); }
 const uint16_t id = high < 0 || low < 0 ? 0 : (high << 8) | low;
 mutex_enter_blocking(&cameraMutex);
 shared.spi = spi; shared.sensorId = id;
 mutex_exit(&cameraMutex);
 if(!spi) { setStatus("SPI_ERROR"); return false; }
 if(!id) { setStatus("I2C_ERROR"); return false; }
 if(id != 0x2641 && id != 0x2642) { setStatus("UNSUPPORTED_SENSOR"); return false; }
 if(!sensorWrite(0xff, 1) || !sensorWrite(0x12, 0x80)) return false;
 delay(100); // Core 1 only; GPS/radio/USB continue on core 0.
 using namespace camera_registers;
 if(!writeTable(OV2640_JPEG_INIT) || !writeTable(OV2640_YUV422) ||
    !writeTable(OV2640_JPEG) || !sensorWrite(0xff, 1) || !sensorWrite(0x15, 0) ||
    !writeTable(OV2640_320x240_JPEG)) { setStatus("INIT_ERROR"); return false; }
 delay(1000); // Let exposure and white balance settle before the first frame.
 writeReg(0x04, 1);
 setStatus("READY"); return true;
}
inline uint32_t crc32(const uint8_t* bytes, uint32_t length) {
 uint32_t crc = 0xffffffff;
 for(uint32_t i = 0; i < length; ++i) {
  crc ^= bytes[i];
  for(unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
 }
 return ~crc;
}
inline void pollSensor() {
 const uint32_t now = millis();
 if(phase == 0) {
  if(uint32_t(now - attemptedAt) < IntervalMs) return;
  mutex_enter_blocking(&cameraMutex); const bool busy = shared.ready; mutex_exit(&cameraMutex);
  if(busy) return;
  attemptedAt = now;
  if(!initialized) { initialized = initialize(); if(!initialized) return; }
  writeReg(0x04, 1); writeReg(0x04, 2);
  phaseAt = millis(); phase = 1; setStatus("CAPTURING");
 } else if(phase == 1) {
  if(uint32_t(now - phaseAt) > 10000) { setStatus("CAPTURE_TIMEOUT"); phase = 0; return; }
  if(!(readReg(0x41) & 8)) return;
  fifoLength = readReg(0x42) | (uint32_t(readReg(0x43)) << 8) | (uint32_t(readReg(0x44) & 0x7f) << 16);
  if(fifoLength < 4 || fifoLength > MaxImage) { setStatus("SIZE_ERROR"); phase = 0; return; }
  readAt = 0; phase = 2;
 } else {
  // Small FIFO reads leave room for core 1's environmental sensor polling.
  for(unsigned n = 0; n < 64 && readAt < fifoLength; ++n) image[readAt++] = readReg(0x3d);
  if(readAt < fifoLength) return;
  uint32_t start = 0, end = 0;
  for(uint32_t i = 1; i < fifoLength; ++i) {
   if(!end && image[i-1] == 0xff && image[i] == 0xd8) { start = i-1; end = i+1; }
   if(end && image[i-1] == 0xff && image[i] == 0xd9) { end = i+1; break; }
  }
  if(end <= start+2 || image[end-2] != 0xff || image[end-1] != 0xd9) {
   setStatus("JPEG_ERROR"); phase = 0; return;
  }
  const uint32_t length = end - start;
  memmove(image, image + start, length);
  const uint32_t checksum = crc32(image, length);
  mutex_enter_blocking(&cameraMutex);
  shared.length = length; shared.crc = checksum; ++shared.sequence;
  shared.status = "CAPTURED"; shared.ready = true;
  mutex_exit(&cameraMutex);
  phase = 0;
 }
}
inline bool sendLine(const char* line, int length) {
 if(!Serial || length <= 0 || usb_output::availableForWrite() < length) return false;
 return Serial.write(reinterpret_cast<const uint8_t*>(line), size_t(length)) == size_t(length);
}
inline void poll() {
 static uint32_t reportAt = 0, transferAt = 0, active = 0, offset = 0;
 static uint8_t stage = 0;
 Shared snapshot;
 if(!mutex_try_enter(&cameraMutex, nullptr)) return;
 snapshot = shared;
 mutex_exit(&cameraMutex);
 const uint32_t now = millis();
 char line[192];
 if(snapshot.ready) {
  if(active != snapshot.sequence) { active = snapshot.sequence; offset = 0; stage = 0; transferAt = now; }
  if(uint32_t(now - transferAt) > 15000) {
   if(mutex_try_enter(&cameraMutex, nullptr)) { shared.ready = false; mutex_exit(&cameraMutex); }
   return;
  }
  int length;
  if(stage == 0) {
   length = snprintf(line, sizeof(line), "CAM_BEGIN id=%lu bytes=%lu crc=%08lX width=320 height=240\r\n",
    (unsigned long)active, (unsigned long)snapshot.length, (unsigned long)snapshot.crc);
   if(sendLine(line, length)) stage = 1;
  } else if(offset < snapshot.length) {
   static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   char encoded[65]; unsigned used = 0;
   const uint32_t count = min(uint32_t(48), snapshot.length-offset);
   // The producer owns this buffer only when ready=false; no writes while sending.
   for(uint32_t i = 0; i < count; i += 3) {
    const uint32_t value = (uint32_t(image[offset+i]) << 16) |
     (i+1 < count ? uint32_t(image[offset+i+1]) << 8 : 0) | (i+2 < count ? image[offset+i+2] : 0);
    encoded[used++] = alphabet[(value >> 18) & 63]; encoded[used++] = alphabet[(value >> 12) & 63];
    encoded[used++] = i+1 < count ? alphabet[(value >> 6) & 63] : '=';
    encoded[used++] = i+2 < count ? alphabet[value & 63] : '=';
   }
   encoded[used] = 0;
   length = snprintf(line, sizeof(line), "CAM_DATA id=%lu offset=%lu data=%s\r\n",
    (unsigned long)active, (unsigned long)offset, encoded);
   if(sendLine(line, length)) offset += count;
  } else {
   length = snprintf(line, sizeof(line), "CAM_END id=%lu\r\n", (unsigned long)active);
   if(sendLine(line, length) && mutex_try_enter(&cameraMutex, nullptr)) {
    shared.ready = false; mutex_exit(&cameraMutex);
   }
  }
 } else if(uint32_t(now - reportAt) >= 5000) {
  const int length = snprintf(line, sizeof(line), "CAM_STATUS %s spi=%u sensor_id=0x%04X frames=%lu\r\n",
   snapshot.status, unsigned(snapshot.spi), unsigned(snapshot.sensorId), (unsigned long)snapshot.sequence);
  if(sendLine(line, length)) reportAt = now;
 }
}
}
