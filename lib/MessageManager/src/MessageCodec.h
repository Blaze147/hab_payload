#pragma once
#include <stdint.h>
#include <stddef.h>

namespace messaging {
// Versioned, compact typed records. All integers are big-endian.
constexpr uint8_t CodecVersion = 0xa1;
enum class DataType : uint8_t {
 InternalTemperature=1, InternalHumidity=2, InternalLowTemperature=3, ExternalRtd=4,
 Pressure=5, Gps=6, Camera=7
};
struct SensorData {
 uint32_t uptimeMs=0;
 // Presence means a fresh, valid measurement; zero is a valid value.
 bool internalTemperature=false, internalHumidity=false, internalLowTemperature=false,
      externalRtd=false, pressure=false, gps=false;
 int16_t internalTemperatureCentiC=0, internalLowTemperatureCentiC=0, externalRtdCentiC=0;
 uint16_t internalHumidityCentiPct=0;
 uint32_t pressurePa=0;
 // GPS flags: stream=1, position=2, altitude=4, satellites=8.
 uint8_t gpsFlags=0;
 int32_t latitudeE6=0, longitudeE6=0, altitudeDm=0;
 uint16_t satellites=0;
};
struct CameraView {
 uint16_t width=0, height=0;
 const uint8_t* jpeg=nullptr;
 size_t length=0;
};
constexpr size_t MaxSensorBytes=64;
// Envelope: A1, uptime:u32, then records (id:u8,length:u8,value).
// length=255 introduces an extended u16 length (for images).
// One record per ID; IDs are explicit so absent sensors cannot shift identity.
size_t encodeSensors(const SensorData&,uint8_t*,size_t);
size_t encodeCamera(const CameraView&,uint32_t uptime,uint8_t*,size_t);
bool decode(const uint8_t*,size_t,SensorData&,CameraView&);
}
