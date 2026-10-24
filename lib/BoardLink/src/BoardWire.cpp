#include "BoardWire.h"
namespace board_link {
namespace {
uint16_t step(uint16_t crc,uint8_t x) {
 crc^=uint16_t(x)<<8;
 for(uint8_t i=0;i<8;++i) crc=uint16_t(crc&0x8000 ? (crc<<1)^0x1021 : crc<<1);
 return crc;
}
}
void Frames::pump() {
 for(int value=io_.readByte();value>=0;value=io_.readByte()) {
  uint8_t x=uint8_t(value);
  if(x==0x7e) {
   if(!discard_ && used_) {
    if(escaped_ || used_<12) ++badFrames;
    else {
     Reader r(rx_,used_);const auto version=r.u8();const auto kind=Kind(r.u8());const auto id=r.u16();const auto session=r.u32();const auto n=r.u16();
     const uint16_t expected=uint16_t(uint16_t(rx_[used_-2])<<8 | rx_[used_-1]);
     uint16_t crc=0xffff;for(size_t i=0;i<used_-2;++i) crc=step(crc,rx_[i]);
     if(version==1 && size_t(n)+12==used_ && crc==expected) { ++receivedFrames;frame(kind,id,session,rx_+10,n); }
     else ++badFrames;
    }
   }
   used_=0;escaped_=false;discard_=false;continue;
  }
  if(discard_) continue;
  if(x==0x7d && !escaped_) { escaped_=true;continue; }
  if(escaped_) { x^=0x20;escaped_=false; }
  if(used_==Capacity) { discard_=true;++badFrames;continue; }
  rx_[used_++]=x;
 }
}
bool Frames::put(uint8_t x) {
 const uint32_t start=io_.now();
 while(!io_.writeByte(x)) {
  pump();io_.progress();if(uint32_t(io_.now()-start)>CommandTimeout) return false;
 }
 // Service incoming bytes even while sending a maximum-length frame.
 pump();return true;
}
bool Frames::send(Kind kind,uint16_t id,uint32_t session,const uint8_t* p,size_t n) {
 if(n>Capacity-12) return false;
 uint8_t head[10];Writer w(head);w.u8(1);w.u8(uint8_t(kind));w.u16(id);w.u32(session);w.u16(uint16_t(n));
 if(!put(0x7e)) return false;
 uint16_t crc=0xffff;
 for(size_t i=0;i<n+12;++i) {
  uint8_t x;
  if(i<10) x=head[i];else if(i<n+10) x=p[i-10];else x=i==n+10 ? uint8_t(crc>>8) : uint8_t(crc);
  if(i<n+10) crc=step(crc,x);
  if((x==0x7e || x==0x7d) && !put(0x7d)) return false;
  if(!put((x==0x7e || x==0x7d) ? uint8_t(x^0x20) : x)) return false;
 }
 if(!put(0x7e)) return false;
 ++sentFrames;return true;
}
}
