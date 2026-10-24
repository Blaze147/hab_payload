#include "ProtocolManager.h"
#include <string.h>
#include "ModulationProfiles.h"

namespace {
uint16_t u16(const uint8_t* p) { return uint16_t(uint16_t(p[0])<<8 | p[1]); }
void put16(uint8_t* p,uint16_t value) { p[0]=uint8_t(value>>8); p[1]=uint8_t(value); }
void dataHeaderBytes(const protocol::Header& h,uint8_t* out) {
 out[0]=h.version; out[1]=uint8_t(h.type); put16(out+2,h.transmitterSessionId);
 put16(out+4,h.messageId); put16(out+6,h.fragmentCount); put16(out+8,h.fragmentIndex);
 out[10]=h.retryCount; out[11]=h.flags; out[12]=h.payloadLength;
}
}
void ProtocolManager::begin() { dataReceiver_=nullptr; linkReceiver_=nullptr; }
packet_statistics::Observation ProtocolManager::observation(const radio::PacketInfo& info) const {
 packet_statistics::Observation o;
 o.receivedAtMs=info.receivedAtMs; o.wireBytes=uint16_t(info.length);
 if(info.hasModulation) {
  auto c=radio_.config(); c.spreadingFactor=info.spreadingFactor;
  c.bandwidth=info.bandwidth; c.codingRate=info.codingRate;
  o.airtimeMs=radio::RadioManager::airtimeMs(c,info.length);
 }
 o.rssi=info.rssi; o.snr=info.snr;
 if(info.hasModulation) o.profile=modulation_profiles::identify(info.spreadingFactor,info.bandwidth,info.codingRate);
 return o;
}
radio::Status ProtocolManager::poll(uint32_t nowMs) {
 const auto before=radio_.state();
 auto result=radio_.poll(nowMs);
 settleRestart();
 if(before!=radio::State::Fault && radio_.state()==radio::State::Fault)
  statistics_.recordRadioError();
 if(result!=radio::Status::Ok || radio_.state()!=radio::State::Receiving) return result;
 radio::PacketInfo info;
 result=radio_.read(fragment_.rawBytes,protocol::MaxPacketSize,info,nowMs);
 if(result==radio::Status::Ok) {
  receiveRaw(fragment_.rawBytes,info.length,info); // Handler result is independent of hardware status.
 } else if(result==radio::Status::CrcError) {
  auto o=observation(info); o.integrity=packet_statistics::Integrity::Corrupt; statistics_.observe(o);
 } else if(result==radio::Status::HardwareError) statistics_.recordRadioError();
 return result==radio::Status::NoPacket ? radio::Status::Ok : result;
}
namespace {
protocol::Result transmitResult(radio::Status status) {
 switch(status) {
 case radio::Status::Ok: return protocol::Result::Ok;
 case radio::Status::Busy: return protocol::Result::Busy;
 case radio::Status::NotInitialized: return protocol::Result::NotReady;
 case radio::Status::InvalidPayload: case radio::Status::InvalidConfig: return protocol::Result::InvalidArgument;
 default: return protocol::Result::HardwareError;
 }
}
void finishPacket(uint8_t* wire,size_t length) { put16(wire+length-2,protocol::crc16(wire,length-2)); }
}
void ProtocolManager::settleRestart() {
 if(restartInFlight_ && radio_.transmission()!=radio::TxStatus::InProgress) {
  // Retain the indication when a locally accepted TX fails or times out.
  if(radio_.transmission()!=radio::TxStatus::Complete) restartPending_=true;
  restartInFlight_=false;
 }
}
protocol::Result ProtocolManager::transmit(uint8_t* wire,size_t length,uint32_t now) {
 settleRestart();
 // Only use existing flag fields. Beacons cannot carry or consume a restart.
 const auto type=protocol::PacketType(wire[1]);
 const bool hasFlags=type==protocol::PacketType::Data || type==protocol::PacketType::Control;
 const size_t offset=type==protocol::PacketType::Data ? protocol::FlagsOffset : protocol::ControlFlagsOffset;
 if(hasFlags) wire[offset]&=uint8_t(~protocol::RestartFlag); // Never echo an RX restart.
 const bool eligible=type==protocol::PacketType::Data ||
  (type==protocol::PacketType::Control && wire[offset]==uint8_t(protocol::ControlFlag::None));
 const bool restarted=restartPending_ && eligible;
 if(restarted) wire[offset]|=protocol::RestartFlag;
 finishPacket(wire,length);
 const auto result=transmitResult(radio_.transmit(wire,length,now));
 if(result==protocol::Result::Ok) {
  lastTxRestarted_=restarted;
  restartInFlight_=restarted;
  if(restarted) restartPending_=false;
 }
 return result;
}
protocol::Result ProtocolManager::sendData(const protocol::DataPacket& p,uint32_t now) {
 return sendData(p.header,p.payload,now);
}
protocol::Result ProtocolManager::sendData(const protocol::Header& header,const uint8_t* payload,uint32_t now) {
 if(!protocol::valid(header) || !payload) return protocol::Result::InvalidArgument;
 if(!dataAllowed_ || callbackActive_ || radio_.state()==radio::State::Transmitting) return protocol::Result::Busy;
 if(dataDeadline_ && (int32_t(now-dataEndMs_)>=0 ||
    airtimeMs(protocol::packetSize(header.payloadLength))>=uint32_t(dataEndMs_-now))) return protocol::Result::Busy;
 // Reuse RX workspace outside callbacks; adapter must consume TX bytes before return.
 dataHeaderBytes(header,fragment_.rawBytes);
 memcpy(fragment_.rawBytes+protocol::PayloadOffset,payload,header.payloadLength);
 const size_t n=protocol::packetSize(header.payloadLength);
 return transmit(fragment_.rawBytes,n,now);
}
protocol::Result ProtocolManager::sendControl(const protocol::ControlPacket& p,uint32_t now) {
 if(!protocol::valid(p)) return protocol::Result::InvalidArgument;
 uint8_t wire[protocol::ControlPacketOverhead+2*protocol::MaxControlBitmapCount];
 wire[0]=p.version; wire[1]=uint8_t(p.type); wire[2]=p.conversationId;
 wire[3]=p.flags; wire[4]=p.profileChangeRequest; wire[5]=uint8_t(p.windowTimingSlide);
 put16(wire+6,p.retransmitMessageId); put16(wire+8,p.retransmitGroupBitmap);
 for(uint8_t i=0;i<p.retransmitBitmapCount;++i) put16(wire+10+2*i,p.retransmitBitmaps[i]);
 const size_t n=protocol::controlPacketSize(p.retransmitBitmapCount);
 return transmit(wire,n,now);
}
protocol::Result ProtocolManager::sendBeacon(const protocol::BeaconPacket& p,uint32_t now) {
 if(!protocol::valid(p)) return protocol::Result::InvalidArgument;
 uint8_t wire[protocol::BeaconPacketSize];
 wire[0]=p.version; wire[1]=uint8_t(p.type); put16(wire+2,p.timeToNextConversation);
 wire[4]=p.conversationDuration;
 return transmit(wire,sizeof(wire),now);
}
protocol::Result ProtocolManager::deliverData(const protocol::Header& header,const radio::PacketInfo& info,
                                              packet_statistics::Integrity integrity) {
 fragment_.header=header; fragment_.reception=info; fragment_.rawLength=info.length;
 auto o=observation(info); fragment_.profile=o.profile; o.integrity=integrity; o.data=true;
 o.sessionId=header.transmitterSessionId; o.messageId=header.messageId;
 o.fragmentIndex=header.fragmentIndex; o.fragmentCount=header.fragmentCount;
 o.payloadBytes=header.payloadLength; o.retryCount=header.retryCount;
 statistics_.observe(o);
 if(!dataReceiver_) return protocol::Result::NotConnected;
 callbackActive_=true; const auto result=dataReceiver_->onFragment(fragment_); callbackActive_=false;
 return result;
}
protocol::Result ProtocolManager::receiveRaw(const uint8_t* bytes,size_t length,const radio::PacketInfo& info) {
 if(!bytes || length>protocol::MaxPacketSize || length!=info.length || length<4) {
  auto o=observation(info); o.integrity=packet_statistics::Integrity::Malformed; statistics_.observe(o);
  return protocol::Result::InvalidArgument;
 }
 // Check CRC before trusting version, packet type, IDs or sequence information.
 if(protocol::crc16(bytes,length-2)!=u16(bytes+length-2)) {
  auto o=observation(info); o.integrity=packet_statistics::Integrity::Corrupt; statistics_.observe(o);
  return protocol::Result::InvalidArgument;
 }
 auto malformed=[&]() {
  auto o=observation(info); o.integrity=packet_statistics::Integrity::Malformed; o.crcChecked=true; statistics_.observe(o);
  return protocol::Result::InvalidArgument;
 };
 if(bytes[0]!=protocol::Version) return malformed();
 const auto type=protocol::PacketType(bytes[1]);
 if(type==protocol::PacketType::Data) {
  if(length<protocol::PacketOverhead) return malformed();
  protocol::Header h;
  h.version=bytes[0]; h.type=type; h.transmitterSessionId=u16(bytes+2);
  h.messageId=u16(bytes+4); h.fragmentCount=u16(bytes+6); h.fragmentIndex=u16(bytes+8);
  h.retryCount=bytes[10]; h.flags=bytes[11]; h.payloadLength=bytes[12];
  if(!protocol::valid(h,length)) return malformed();
  memmove(fragment_.rawBytes,bytes,length);
  return deliverData(h,info,packet_statistics::Integrity::Valid);
 }
 if(type==protocol::PacketType::Beacon) {
  if(length!=protocol::BeaconPacketSize) return malformed();
  protocol::BeaconPacket p; p.timeToNextConversation=u16(bytes+2); p.conversationDuration=bytes[4];
  p.crc=u16(bytes+5);
  if(!protocol::valid(p,length)) return malformed();
  auto o=observation(info); o.integrity=packet_statistics::Integrity::Valid; statistics_.observe(o);
  return linkReceiver_ ? linkReceiver_->onBeacon(p,info) : protocol::Result::NotConnected;
 }
 if(type==protocol::PacketType::Control) {
  if(length<protocol::ControlPacketOverhead) return malformed();
  protocol::ControlPacket p;
  p.conversationId=bytes[2]; p.flags=bytes[3]; p.profileChangeRequest=bytes[4];
  p.windowTimingSlide=bytes[5]<128 ? int8_t(bytes[5]) : int8_t(int16_t(bytes[5])-256);
  p.retransmitMessageId=u16(bytes+6); p.retransmitGroupBitmap=u16(bytes+8);
  p.retransmitBitmapCount=protocol::controlBitmapCount(p.retransmitGroupBitmap);
  if(!protocol::valid(p,length)) return malformed();
  for(uint8_t i=0;i<p.retransmitBitmapCount;++i) p.retransmitBitmaps[i]=u16(bytes+10+2*i);
  p.crc=u16(bytes+length-2);
  auto o=observation(info); o.integrity=packet_statistics::Integrity::Valid; statistics_.observe(o);
  return linkReceiver_ ? linkReceiver_->onControl(p,info) : protocol::Result::NotConnected;
 }
 return malformed();
}
protocol::Result ProtocolManager::acceptDecoded(const protocol::DataPacket& p,const radio::PacketInfo& info) {
 if(!protocol::valid(p.header,info.length)) {
  auto o=observation(info); o.integrity=packet_statistics::Integrity::Malformed; statistics_.observe(o);
  return protocol::Result::InvalidArgument;
 }
 dataHeaderBytes(p.header,fragment_.rawBytes);
 memcpy(fragment_.rawBytes+protocol::PayloadOffset,p.payload,p.header.payloadLength);
 put16(fragment_.rawBytes+protocol::crcOffset(p.header.payloadLength),p.crc);
 return deliverData(p.header,info,packet_statistics::Integrity::Unchecked);
}
protocol::Result ProtocolManager::acceptDecoded(const protocol::ControlPacket& p,const radio::PacketInfo& info) {
 if(!protocol::valid(p,info.length)) {
  auto o=observation(info); o.integrity=packet_statistics::Integrity::Malformed; statistics_.observe(o);
  return protocol::Result::InvalidArgument;
 }
 statistics_.observe(observation(info));
 return linkReceiver_ ? linkReceiver_->onControl(p,info) : protocol::Result::NotConnected;
}
protocol::Result ProtocolManager::acceptDecoded(const protocol::BeaconPacket& p,const radio::PacketInfo& info) {
 if(!protocol::valid(p,info.length)) {
  auto o=observation(info); o.integrity=packet_statistics::Integrity::Malformed; statistics_.observe(o);
  return protocol::Result::InvalidArgument;
 }
 statistics_.observe(observation(info));
 return linkReceiver_ ? linkReceiver_->onBeacon(p,info) : protocol::Result::NotConnected;
}
