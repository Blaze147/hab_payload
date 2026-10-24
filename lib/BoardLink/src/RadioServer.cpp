#include "RadioServer.h"
namespace board_link {
void RadioServer::begin() {
 protocol_.begin();protocol_.setDataReceiver(this);protocol_.setLinkReceiver(this);protocol_.allowDataTransmission(false);
}
void RadioServer::frame(Kind kind,uint16_t id,uint32_t session,const uint8_t* p,size_t n) {
 if(kind!=Kind::Command || !n || n>sizeof(request_)) { ++rejectedCommands;return; }
 if(pending_) { ++rejectedCommands;return; }
 memcpy(request_,p,n);requestLength_=n;requestId_=id;requestSession_=session;pending_=true;
}
void RadioServer::status(Kind kind,uint16_t id,uint8_t result) {
 Writer w(scratch_);w.u8(result);w.u32(io_.now());w.u8(uint8_t(radio_.state()));w.u8(uint8_t(radio_.transmission()));config(w,radio_.config());w.u8(protocol_.lastTransmissionRestarted());
 send(kind,id,session_,scratch_,w.length);
}
void RadioServer::execute() {
 Reader r(request_,requestLength_);const auto op=Op(r.u8());
 if(op==Op::Hello) {
  if(!r.done() || !requestSession_) { ++rejectedCommands;return; }
  session_=requestSession_;connected_=true;haveLast_=false;eventSequence_=0;protocol_.allowDataTransmission(false);
 } else if(!connected_ || requestSession_!=session_) { ++rejectedCommands;return; }
 lastContact_=io_.now();
 if(haveLast_ && requestId_==lastId_) { ++duplicateCommands;status(Kind::Reply,requestId_,lastResult_);return; }
 // Monotonic, wrap-safe IDs: stale requests cannot reconfigure or transmit.
 if(haveLast_ && int16_t(requestId_-lastId_)<=0) { ++rejectedCommands;return; }
 uint8_t result=uint8_t(radio::Status::InvalidPayload);
 switch(op) {
 case Op::MarkRestart:
  if(r.done()) { protocol_.markRestart();result=uint8_t(radio::Status::Ok); }break;
 case Op::Hello: case Op::Status:
  if(r.done()) result=uint8_t(radio::Status::Ok);break;
 case Op::Initialize: case Op::Configure: {
  radio::Config c;config(r,c);
  if(r.done()) result=uint8_t(op==Op::Initialize ? radio_.initialize(c) : protocol_.configureRadio(c));break;
 }
 case Op::Receive: if(r.done()) result=uint8_t(protocol_.receive());break;
 case Op::Idle: if(r.done()) result=uint8_t(protocol_.idle());break;
 case Op::Gate: {
  const auto mode=r.u8();const uint32_t deadline=r.u32();
  if(r.done() && mode<=2) {
   if(mode==2) protocol_.setDataWindow(deadline);else protocol_.allowDataTransmission(mode==1);
   result=uint8_t(radio::Status::Ok);
  }break;
 }
 case Op::SendData: {
  protocol::Header h;header(r,h);
  if(r.ok && protocol::valid(h) && r.length-r.at==h.payloadLength) result=uint8_t(protocol_.sendData(h,r.data+r.at,io_.now()));
  else result=uint8_t(protocol::Result::InvalidArgument);break;
 }
 case Op::SendControl: {
  protocol::ControlPacket p;control(r,p);result=uint8_t(r.done() ? protocol_.sendControl(p,io_.now()) : protocol::Result::InvalidArgument);break;
 }
#ifdef SPLIT_DIAGNOSTICS
 case Op::Diagnostic: {
  const auto mode=r.u8();
  if(!r.done() || mode>2) { result=uint8_t(protocol::Result::InvalidArgument);break; }
  // Exercise the real decoder/CRC/event path without synthesizing RF reception.
  protocol::Header h;h.transmitterSessionId=0x1234;h.messageId=requestId_;h.payloadLength=240;
  Writer w(request_);header(w,h);for(uint16_t i=0;i<240;++i) w.u8(uint8_t(i));
  if(mode==2) request_[1]=99;
  const uint16_t crc=protocol::crc16(request_,253);w.u16(crc);
  if(mode==1) request_[20]^=1;
  radio::PacketInfo i;i.length=255;i.receivedAtMs=io_.now();i.rssi=-91;i.snr=7.25f;
  i.hasModulation=true;i.spreadingFactor=radio_.config().spreadingFactor;i.codingRate=radio_.config().codingRate;i.bandwidth=radio_.config().bandwidth;
  result=uint8_t(protocol_.receiveRaw(request_,255,i));break;
 }
#endif
 case Op::SendBeacon: {
  protocol::BeaconPacket p;beacon(r,p);result=uint8_t(r.done() ? protocol_.sendBeacon(p,io_.now()) : protocol::Result::InvalidArgument);break;
 }
 default: break;
 }
 ++commands;lastId_=requestId_;lastResult_=result;haveLast_=true;status(Kind::Reply,requestId_,result);
}
void RadioServer::poll() {
 pump();
 // Keep the mailbox occupied during execution and response serialization.
 if(pending_) { execute();pending_=false; }
 const uint32_t now=io_.now();
 if(connected_ && uint32_t(now-lastContact_)>LeaseMs) {
  connected_=false;protocol_.allowDataTransmission(false);
 }
 if(!connected_ && radio_.state()!=radio::State::Transmitting && radio_.state()!=radio::State::Idle && radio_.state()!=radio::State::Uninitialized && radio_.state()!=radio::State::Fault) protocol_.idle();
 protocol_.poll(now);
 if(connected_ && (radio_.state()!=lastState_ || radio_.transmission()!=lastTx_)) {
  lastState_=radio_.state();lastTx_=radio_.transmission();status(Kind::State,++eventSequence_,0);
 }
}
void RadioServer::event(Kind kind,Writer& w) { if(connected_ && w.ok) send(kind,++eventSequence_,session_,scratch_,w.length); }
void RadioServer::observe(const packet_statistics::Observation& o) { Writer w(scratch_);observation(w,o);event(Kind::Observation,w); }
void RadioServer::recordRadioError() { Writer w(scratch_);event(Kind::RadioError,w); }
protocol::Result RadioServer::onFragment(const protocol::Fragment& p) {
 Writer w(scratch_);info(w,p.reception);header(w,p.header);w.bytes(p.rawBytes,p.rawLength);event(Kind::Data,w);return protocol::Result::Ok;
}
protocol::Result RadioServer::onControl(const protocol::ControlPacket& p,const radio::PacketInfo& i) {
 Writer w(scratch_);info(w,i);control(w,p);event(Kind::Control,w);return protocol::Result::Ok;
}
protocol::Result RadioServer::onBeacon(const protocol::BeaconPacket& p,const radio::PacketInfo& i) {
 Writer w(scratch_);info(w,i);beacon(w,p);event(Kind::Beacon,w);return protocol::Result::Ok;
}
}
