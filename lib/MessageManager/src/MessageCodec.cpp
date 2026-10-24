#include "MessageCodec.h"
#include <string.h>
namespace messaging {
namespace {
void put16(uint8_t* p,uint16_t v) { p[0]=uint8_t(v>>8);p[1]=uint8_t(v); }
void put32(uint8_t* p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(24-8*i)); }
uint16_t u16(const uint8_t* p) { return uint16_t(p[0])<<8|p[1]; }
uint32_t u32(const uint8_t* p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
int16_t s16(const uint8_t* p) { const uint16_t v=u16(p);return int16_t(v<32768?int32_t(v):int32_t(v)-65536); }
int32_t s32(const uint8_t* p) { const uint32_t v=u32(p);return v<0x80000000UL?int32_t(v):int32_t(int64_t(v)-4294967296LL); }
bool valid(const SensorData& s) {
 return (!s.internalTemperature || (s.internalTemperatureCentiC>=-4000 && s.internalTemperatureCentiC<=8000)) &&
 (!s.internalHumidity || s.internalHumidityCentiPct<=10000) &&
 (!s.internalLowTemperature || (s.internalLowTemperatureCentiC>=-5500 && s.internalLowTemperatureCentiC<=12500)) && (!s.externalRtd || s.externalRtdCentiC>=-20000) &&
 (!s.pressure || (s.pressurePa>0 && s.pressurePa<=200000)) &&
 (!s.gps || (!(s.gpsFlags&~15) && (!(s.gpsFlags&2) ||
  (s.latitudeE6>=-90000000 && s.latitudeE6<=90000000 && s.longitudeE6>=-180000000 && s.longitudeE6<=180000000))));
}
}
size_t encodeSensors(const SensorData& s,uint8_t* out,size_t cap) {
 if(!out || cap<MaxSensorBytes || !valid(s))return 0;
 out[0]=CodecVersion;put32(out+1,s.uptimeMs);size_t n=5;
 auto small=[&](DataType id,uint16_t v) { out[n++]=uint8_t(id);out[n++]=2;put16(out+n,v);n+=2; };
 if(s.internalTemperature)small(DataType::InternalTemperature,uint16_t(s.internalTemperatureCentiC));
 if(s.internalHumidity)small(DataType::InternalHumidity,s.internalHumidityCentiPct);
 if(s.internalLowTemperature)small(DataType::InternalLowTemperature,uint16_t(s.internalLowTemperatureCentiC));
 if(s.externalRtd)small(DataType::ExternalRtd,uint16_t(s.externalRtdCentiC));
 if(s.pressure) { out[n++]=uint8_t(DataType::Pressure);out[n++]=4;put32(out+n,s.pressurePa);n+=4; }
 if(s.gps) {
  out[n++]=uint8_t(DataType::Gps);out[n++]=15;out[n++]=s.gpsFlags;
  put32(out+n,uint32_t(s.latitudeE6));n+=4;put32(out+n,uint32_t(s.longitudeE6));n+=4;
  put32(out+n,uint32_t(s.altitudeDm));n+=4;put16(out+n,s.satellites);n+=2;
 }
 return n;
}
size_t encodeCamera(const CameraView& c,uint32_t uptime,uint8_t* out,size_t cap) {
 const size_t size=13+c.length;
 if(!out || !c.jpeg || c.length<4 || c.length>32768 || !c.width || !c.height || cap<size ||
 c.jpeg[0]!=0xff || c.jpeg[1]!=0xd8 || c.jpeg[c.length-2]!=0xff || c.jpeg[c.length-1]!=0xd9)return 0;
 out[0]=CodecVersion;put32(out+1,uptime);out[5]=uint8_t(DataType::Camera);out[6]=255;
 put16(out+7,uint16_t(c.length+4));put16(out+9,c.width);put16(out+11,c.height);
 memcpy(out+13,c.jpeg,c.length);return size;
}
bool decode(const uint8_t* p,size_t size,SensorData& s,CameraView& camera) {
 s=SensorData{};camera=CameraView{};
 if(!p || size<5 || p[0]!=CodecVersion)return false;
 s.uptimeMs=u32(p+1);size_t at=5;uint8_t seen=0;
 while(at<size) {
  if(size-at<2)return false;
  const uint8_t id=p[at++];size_t length=p[at++];
  if(length==255) { if(size-at<2)return false;length=u16(p+at);at+=2; }
  if(length>size-at || id<1 || id>7 || (seen&(1u<<(id-1))))return false;
  seen|=1u<<(id-1);const uint8_t* v=p+at;at+=length;
  if(id<=4) {
   if(length!=2)return false;
   if(id==1){s.internalTemperature=true;s.internalTemperatureCentiC=s16(v);}
   if(id==2){s.internalHumidity=true;s.internalHumidityCentiPct=u16(v);}
   if(id==3){s.internalLowTemperature=true;s.internalLowTemperatureCentiC=s16(v);}
   if(id==4){s.externalRtd=true;s.externalRtdCentiC=s16(v);}
  } else if(id==5) {
   if(length!=4)return false;
   s.pressure=true;s.pressurePa=u32(v);
  } else if(id==6) {
   if(length!=15)return false;
   s.gps=true;s.gpsFlags=v[0];s.latitudeE6=s32(v+1);s.longitudeE6=s32(v+5);
   s.altitudeDm=s32(v+9);s.satellites=u16(v+13);
  } else {
   if(length<8)return false;
   camera.width=u16(v);camera.height=u16(v+2);camera.jpeg=v+4;camera.length=length-4;
   if(!camera.width || !camera.height || camera.length>32768 || camera.jpeg[0]!=0xff ||
    camera.jpeg[1]!=0xd8 || camera.jpeg[camera.length-2]!=0xff || camera.jpeg[camera.length-1]!=0xd9)return false;
  }
 }
 return (!camera.jpeg || seen==(1u<<6)) && valid(s);
}
}
