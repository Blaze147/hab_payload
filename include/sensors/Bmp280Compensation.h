#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cmath>

// Bosch BMP280 data sheet BST-BMP280-DS001, section 4.2.3:
// factory calibration and double-precision temperature/pressure compensation.
// https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf
namespace bmp280 {
struct Calibration {
 uint16_t t1=0,p1=0;
 int16_t t2=0,t3=0,p2=0,p3=0,p4=0,p5=0,p6=0,p7=0,p8=0,p9=0;
};
inline uint16_t u16(const uint8_t* p) { return uint16_t(p[0])|(uint16_t(p[1])<<8); }
inline int16_t s16(const uint8_t* p) {
 const uint16_t v=u16(p);return int16_t(v<32768?int32_t(v):int32_t(v)-65536);
}
inline bool calibration(const uint8_t* b,size_t size,Calibration& c) {
 if(!b || size!=24)return false;
 c.t1=u16(b);c.t2=s16(b+2);c.t3=s16(b+4);c.p1=u16(b+6);
 c.p2=s16(b+8);c.p3=s16(b+10);c.p4=s16(b+12);c.p5=s16(b+14);
 c.p6=s16(b+16);c.p7=s16(b+18);c.p8=s16(b+20);c.p9=s16(b+22);
 return c.t1 && c.p1 && c.t1!=0xffff && c.p1!=0xffff;
}
inline uint32_t raw20(const uint8_t* b) {
 return (uint32_t(b[0])<<12)|(uint32_t(b[1])<<4)|(b[2]>>4);
}
inline bool compensate(const Calibration& c,uint32_t rawT,uint32_t rawP,double& temperature,double& pressure) {
 if(!c.p1 || rawT>0xfffff || rawP>0xfffff || rawT==0x80000 || rawP==0x80000)return false;
 const double t=double(rawT)/131072.0-double(c.t1)/8192.0;
 const double fine=(double(rawT)/16384.0-double(c.t1)/1024.0)*c.t2+t*t*c.t3;
 temperature=fine/5120.0;
 const double x=fine/2.0-64000.0;
 const double offset=(x*x*c.p6/32768.0+2.0*x*c.p5)/4.0+65536.0*c.p4;
 const double scale=(1.0+(c.p3*x*x/524288.0+c.p2*x)/524288.0/32768.0)*c.p1;
 if(scale==0)return false;
 pressure=(1048576.0-double(rawP)-offset/4096.0)*6250.0/scale;
 pressure+=(c.p9*pressure*pressure/2147483648.0+c.p8*pressure/32768.0+c.p7)/16.0;
 return std::isfinite(temperature) && std::isfinite(pressure);
}
}
