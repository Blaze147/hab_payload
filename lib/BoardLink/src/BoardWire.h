#pragma once
#include <ProtocolTypes.h>
#include <string.h>
namespace board_link {
constexpr uint32_t Baud = 100000; // Exact at 8 MHz AVR; 115200 failed live UART framing tests.
constexpr size_t Capacity = 320;
constexpr uint32_t CommandTimeout = 350, LeaseMs = 1500;
enum class Kind : uint8_t { Command=1, Reply, Observation, Data, Control, Beacon, State, RadioError };
enum class Op : uint8_t { Hello=1, Initialize, Status, Configure, Receive, Idle, Gate, SendData, SendControl, SendBeacon, Diagnostic };
struct IO {
 virtual ~IO() = default;
 virtual int readByte() = 0;
 virtual bool writeByte(uint8_t) = 0; // false means retry after servicing RX.
 virtual uint32_t now() const = 0;
 virtual void progress() = 0;
};
struct Writer {
 uint8_t* data; const size_t capacity; size_t length=0; bool ok=true;
 template<size_t N> explicit Writer(uint8_t (&p)[N]):data(p),capacity(N) {}
 void u8(uint8_t x) { if(length<capacity) data[length++]=x; else ok=false; }
 void u16(uint16_t x) { u8(uint8_t(x>>8));u8(uint8_t(x)); }
 void u32(uint32_t x) { u16(uint16_t(x>>16));u16(uint16_t(x)); }
 void bytes(const uint8_t* p,size_t n) { for(size_t i=0;i<n;++i) u8(p[i]); }
};
struct Reader {
 const uint8_t* data; size_t length, at=0; bool ok=true;
 Reader(const uint8_t* p,size_t n):data(p),length(n) {}
 uint8_t u8() { if(at<length) return data[at++]; ok=false; return 0; }
 uint16_t u16() { const uint16_t a=u8(); return uint16_t(a<<8 | u8()); }
 uint32_t u32() { const uint32_t a=u16(); return a<<16 | u16(); }
 bool done() const { return ok && at==length; }
};
inline void config(Writer& w,const radio::Config& c) {
 w.u32(c.frequency); w.u32(c.bandwidth);w.u8(c.spreadingFactor);w.u8(c.codingRate);
 w.u8(uint8_t(c.power));w.u16(c.preamble);w.u8(c.crc);w.u8(c.syncWord);
}
inline void config(Reader& r,radio::Config& c) {
 c.frequency=r.u32();c.bandwidth=r.u32();c.spreadingFactor=r.u8();c.codingRate=r.u8();
 c.power=int8_t(r.u8());c.preamble=r.u16();c.crc=r.u8()!=0;c.syncWord=r.u8();
}
inline void header(Writer& w,const protocol::Header& h) {
 w.u8(h.version);w.u8(uint8_t(h.type));w.u16(h.transmitterSessionId);w.u16(h.messageId);
 w.u16(h.fragmentCount);w.u16(h.fragmentIndex);w.u8(h.retryCount);w.u8(h.flags);w.u8(h.payloadLength);
}
inline void header(Reader& r,protocol::Header& h) {
 h.version=r.u8();h.type=protocol::PacketType(r.u8());h.transmitterSessionId=r.u16();h.messageId=r.u16();
 h.fragmentCount=r.u16();h.fragmentIndex=r.u16();h.retryCount=r.u8();h.flags=r.u8();h.payloadLength=r.u8();
}
inline void control(Writer& w,const protocol::ControlPacket& p) {
 w.u8(p.version);w.u8(uint8_t(p.type));w.u8(p.conversationId);w.u8(p.flags);w.u8(p.profileChangeRequest);
 w.u8(uint8_t(p.windowTimingSlide));w.u16(p.retransmitMessageId);w.u16(p.retransmitGroupBitmap);
 w.u8(p.retransmitBitmapCount);for(uint8_t i=0;i<p.retransmitBitmapCount && i<16;++i) w.u16(p.retransmitBitmaps[i]);
}
inline void control(Reader& r,protocol::ControlPacket& p) {
 p.version=r.u8();p.type=protocol::PacketType(r.u8());p.conversationId=r.u8();p.flags=r.u8();p.profileChangeRequest=r.u8();
 p.windowTimingSlide=int8_t(r.u8());p.retransmitMessageId=r.u16();p.retransmitGroupBitmap=r.u16();
 p.retransmitBitmapCount=r.u8();if(p.retransmitBitmapCount>16) { r.ok=false;return; }
 for(uint8_t i=0;i<p.retransmitBitmapCount;++i) p.retransmitBitmaps[i]=r.u16();
}
inline void beacon(Writer& w,const protocol::BeaconPacket& p) { w.u8(p.version);w.u8(uint8_t(p.type));w.u16(p.timeToNextConversation);w.u8(p.conversationDuration); }
inline void beacon(Reader& r,protocol::BeaconPacket& p) { p.version=r.u8();p.type=protocol::PacketType(r.u8());p.timeToNextConversation=r.u16();p.conversationDuration=r.u8(); }
inline void info(Writer& w,const radio::PacketInfo& p) {
 w.u16(uint16_t(p.length));w.u16(uint16_t(p.rssi));w.u16(uint16_t(int16_t(p.snr*4)));w.u32(p.receivedAtMs);
 w.u8(p.hasModulation);w.u8(p.spreadingFactor);w.u8(p.codingRate);w.u32(p.bandwidth);
}
inline void info(Reader& r,radio::PacketInfo& p) {
 p.length=r.u16();p.rssi=int16_t(r.u16());p.snr=float(int16_t(r.u16()))/4;p.receivedAtMs=r.u32();
 p.hasModulation=r.u8()!=0;p.spreadingFactor=r.u8();p.codingRate=r.u8();p.bandwidth=r.u32();
}
inline void observation(Writer& w,const packet_statistics::Observation& o) {
 w.u32(o.receivedAtMs);w.u16(o.wireBytes);w.u32(o.airtimeMs);w.u16(uint16_t(o.rssi));w.u16(uint16_t(int16_t(o.snr*4)));
 w.u8(o.profile);w.u8(uint8_t(o.integrity));w.u8(o.crcChecked);w.u8(o.data);w.u16(o.sessionId);w.u16(o.messageId);
 w.u16(o.fragmentIndex);w.u16(o.fragmentCount);w.u8(o.payloadBytes);w.u8(o.retryCount);
}
inline void observation(Reader& r,packet_statistics::Observation& o) {
 o.receivedAtMs=r.u32();o.wireBytes=r.u16();o.airtimeMs=r.u32();o.rssi=int16_t(r.u16());o.snr=float(int16_t(r.u16()))/4;
 o.profile=r.u8();o.integrity=packet_statistics::Integrity(r.u8());o.crcChecked=r.u8()!=0;o.data=r.u8()!=0;
 o.sessionId=r.u16();o.messageId=r.u16();o.fragmentIndex=r.u16();o.fragmentCount=r.u16();o.payloadBytes=r.u8();o.retryCount=r.u8();
}
class Frames {
public:
 explicit Frames(IO& io):io_(io) {}
 void pump();
 uint32_t badFrames=0, receivedFrames=0, sentFrames=0;
protected:
 IO& io_;
 bool send(Kind,uint16_t,uint32_t,const uint8_t*,size_t);
 virtual void frame(Kind,uint16_t,uint32_t,const uint8_t*,size_t)=0;
private:
 uint8_t rx_[Capacity]; size_t used_=0; bool escaped_=false, discard_=true;
 bool put(uint8_t);
};
}
