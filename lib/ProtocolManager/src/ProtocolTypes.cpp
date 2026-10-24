#include "ProtocolTypes.h"
#include <string.h>
namespace protocol {
bool valid(const BeaconPacket& p) {
 return p.version == Version && p.type == PacketType::Beacon &&
        p.conversationDuration > 0 && p.conversationDuration <= 120;
}
bool valid(const BeaconPacket& p, size_t wireLength) {
 return valid(p) && wireLength == BeaconPacketSize;
}
uint8_t controlBitmapCount(uint16_t groups) {
 uint8_t count = 0;
 while(groups) { count += groups & 1; groups >>= 1; }
 return count;
}
bool valid(const ControlPacket& p) {
 return p.version == Version && p.type == PacketType::Control &&
        p.retransmitBitmapCount <= MaxControlBitmapCount &&
        p.retransmitBitmapCount == controlBitmapCount(p.retransmitGroupBitmap);
}
bool valid(const ControlPacket& p, size_t wireLength) {
 return valid(p) && wireLength == controlPacketSize(p.retransmitBitmapCount);
}
bool validMessageSize(size_t length) { return length > 0 && length <= MaxMessageSize; }
bool valid(const Header& h) {
 if(h.version != Version || h.type != PacketType::Data ||
    h.fragmentCount == 0 || h.fragmentCount > MaxFragmentCount ||
    h.fragmentIndex >= h.fragmentCount || h.payloadLength == 0 ||
    h.payloadLength > MaxPayloadSize) return false;
 const size_t prefix = size_t(h.fragmentCount - 1) * MaxPayloadSize;
 if(h.fragmentIndex == h.fragmentCount - 1)
  return prefix + h.payloadLength <= MaxMessageSize;
 return h.payloadLength == MaxPayloadSize;
}
bool valid(const Header& h, size_t wireLength) {
 return valid(h) && wireLength == packetSize(h.payloadLength);
}
}

uint16_t protocol::crc16(const uint8_t* bytes,size_t length) {
 uint16_t crc=CrcInitial;
 for(size_t i=0;i<length;++i) {
  crc ^= uint16_t(bytes[i])<<8;
  for(uint8_t bit=0;bit<8;++bit) crc=uint16_t((crc&0x8000) ? (crc<<1)^CrcPolynomial : crc<<1);
 }
 return crc;
}
