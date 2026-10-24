#include "RemoteProtocol.h"
#include <ModulationProfiles.h>
namespace board_link {
void RemoteProtocol::lost() {
 if(waiting_) aborted_=true;
 if(connected_) stats_.recordRadioError();
 connected_=false;state_=radio::State::Fault;tx_=radio::TxStatus::Failed;
}
void RemoteProtocol::frame(Kind kind,uint16_t id,uint32_t session,const uint8_t* p,size_t n) {
 if(session!=session_) { ++staleFrames;return; }
 if(kind!=Kind::Reply) {
  if(id!=uint16_t(eventSequence_+1)) { ++eventGaps;lost();return; }
  eventSequence_=id;
 }
 if(aborted_ && waiting_) return;
 if(kind==Kind::Reply || kind==Kind::State) {
  if(kind==Kind::Reply && (!waiting_ || id!=waitingId_)) { ++staleFrames;return; }
  Reader r(p,n);const uint8_t result=r.u8();const uint32_t stamp=r.u32();const auto state=radio::State(r.u8());const auto tx=radio::TxStatus(r.u8());radio::Config c;config(r,c);
  // Legacy radio firmware ends after config. The new trailing TX flag also
  // advertises support for MarkRestart, allowing RP2040-only deployments.
  const bool supportsRestart=r.ok && r.at<r.length;
  const uint8_t restarted=supportsRestart ? r.u8() : 0;
  if(!r.done() || restarted>1 || uint8_t(state)>uint8_t(radio::State::Fault) || uint8_t(tx)>uint8_t(radio::TxStatus::Failed)) { ++badFrames;return; }
  state_=state;tx_=tx;config_=c;lastTxRestarted_=restarted!=0;restartSupported_=supportsRestart;
  if(kind==Kind::Reply) { result_=result;remoteStamp_=stamp;replied_=true; }
  return;
 }
 if(!connected_) return;
 if(kind<Kind::Observation || kind>Kind::RadioError) { ++badFrames;return; }
 if(eventCount_==16) { ++overflows;lost();return; }
 auto& e=events_[(eventHead_+eventCount_)%16];e.kind=kind;e.length=uint16_t(n);memcpy(e.bytes,p,n);++eventCount_;
}
bool RemoteProtocol::request(Writer& w) {
 if(waiting_ || !w.ok) return false;
 waiting_=true;replied_=false;aborted_=false;waitingId_=++nextId_;
 const auto op=Op(scratch_[0]);
 const uint32_t start=io_.now();
 const bool sent=send(Kind::Command,waitingId_,session_,scratch_,w.length);
 while(sent && !replied_ && !aborted_ && uint32_t(io_.now()-start)<CommandTimeout) { pump();io_.progress(); }
 waiting_=false;
 if(aborted_) return false;
 if(!sent || !replied_) {
  ++timeouts;lastTimeoutOp=uint8_t(op);lastTimeoutAtMs=io_.now();
  lost();return false;
 }
 const uint32_t finished=io_.now();
 if(uint32_t(finished-start)>maxCommandRttMs) maxCommandRttMs=finished-start;
 // Status timestamp is captured before serializing its reply. Half the RTT is
 // an approximation; deadlines subtract the measured uncertainty as a guard.
 if(op==Op::Hello || op==Op::Status) {
  lastSync_=finished;
  const uint32_t elapsed=finished-start;clockUncertaintyMs=elapsed/2+2;
  offset_=remoteStamp_-(start+elapsed/2);
 }
 return true;
}
bool RemoteProtocol::connect(uint32_t session) {
 connected_=false;session_=session ? session : 1;nextId_=0;eventSequence_=0;eventHead_=eventCount_=0;
 Writer w(scratch_);w.u8(uint8_t(Op::Hello));
 if(!request(w) || result_!=uint8_t(radio::Status::Ok)) return false;
 connected_=true;
 if(restartPending_ && restartSupported_ && markRestart()!=radio::Status::Ok) { lost();return false; }
 return true;
}
radio::Status RemoteProtocol::markRestart() {
 restartPending_=true;
 if(!connected_) return radio::Status::NotInitialized;
 if(!restartSupported_) return radio::Status::InvalidPayload;
 const auto result=simple(Op::MarkRestart);
 if(result==radio::Status::Ok) restartPending_=false;
 return result;
}
radio::Status RemoteProtocol::initialize(const radio::Config& c) {
 Writer w(scratch_);w.u8(uint8_t(Op::Initialize));config(w,c);
 return request(w) ? radio::Status(result_) : radio::Status::HardwareError;
}
radio::Status RemoteProtocol::simple(Op op) {
 if(!connected_) return radio::Status::NotInitialized;
 Writer w(scratch_);w.u8(uint8_t(op));return request(w) ? radio::Status(result_) : radio::Status::HardwareError;
}
radio::Status RemoteProtocol::receive() { return simple(Op::Receive); }
radio::Status RemoteProtocol::idle() { return simple(Op::Idle); }
radio::Status RemoteProtocol::configureRadio(const radio::Config& c) {
 if(!connected_) return radio::Status::NotInitialized;
 Writer w(scratch_);w.u8(uint8_t(Op::Configure));config(w,c);return request(w) ? radio::Status(result_) : radio::Status::HardwareError;
}
void RemoteProtocol::allowDataTransmission(bool allow) {
 if(!connected_) return;
 Writer w(scratch_);w.u8(uint8_t(Op::Gate));w.u8(allow ? 1 : 0);w.u32(0);if(!request(w) || result_!=uint8_t(radio::Status::Ok)) lost();
}
void RemoteProtocol::setDataWindow(uint32_t end) {
 if(!connected_) return;
 Writer w(scratch_);w.u8(uint8_t(Op::Gate));w.u8(2);w.u32(end+offset_-clockUncertaintyMs);
 if(!request(w) || result_!=uint8_t(radio::Status::Ok)) lost();
}
void RemoteProtocol::notifyActivity(protocol::PacketType type,bool tx,uint32_t stamp,size_t length,uint8_t flags,const radio::PacketInfo* info,bool restarted) {
 if(!activity_) return;
 auto c=config_;
 if(info && info->hasModulation) {
  c.spreadingFactor=info->spreadingFactor;c.bandwidth=info->bandwidth;c.codingRate=info->codingRate;
 }
 PacketActivity a;a.type=type;a.transmitting=tx;a.flags=flags;a.restarted=restarted;
 a.durationMs=radio::RadioManager::airtimeMs(c,length);
 a.startMs=tx ? stamp : stamp-a.durationMs;
 a.profile=modulation_profiles::identify(c.spreadingFactor,c.bandwidth,c.codingRate);
 activity_->onPacketActivity(a);
}
protocol::Result RemoteProtocol::sendData(const protocol::Header& h,const uint8_t* p,uint32_t) {
 if(!connected_) return protocol::Result::NotReady;
 if(!protocol::valid(h) || !p) return protocol::Result::InvalidArgument;
 if(dispatching_) return protocol::Result::Busy;
 Writer w(scratch_);w.u8(uint8_t(Op::SendData));header(w,h);w.bytes(p,h.payloadLength);
 const auto result=request(w) ? protocol::Result(result_) : protocol::Result::HardwareError;
 if(result==protocol::Result::Ok) notifyActivity(protocol::PacketType::Data,true,remoteStamp_-offset_,protocol::packetSize(h.payloadLength),uint8_t((h.flags & ~protocol::RestartFlag) | (lastTxRestarted_ ? protocol::RestartFlag : 0)),nullptr,lastTxRestarted_);
 return result;
}
protocol::Result RemoteProtocol::sendControl(const protocol::ControlPacket& p,uint32_t) {
 if(!connected_) return protocol::Result::NotReady;
 if(!protocol::valid(p)) return protocol::Result::InvalidArgument;
 Writer w(scratch_);w.u8(uint8_t(Op::SendControl));control(w,p);const auto result=request(w) ? protocol::Result(result_) : protocol::Result::HardwareError;
 if(result==protocol::Result::Ok) notifyActivity(protocol::PacketType::Control,true,remoteStamp_-offset_,protocol::controlPacketSize(p.retransmitBitmapCount),uint8_t((p.flags & ~protocol::RestartFlag) | (lastTxRestarted_ ? protocol::RestartFlag : 0)),nullptr,lastTxRestarted_);
 return result;
}
protocol::Result RemoteProtocol::sendBeacon(const protocol::BeaconPacket& p,uint32_t) {
 if(!connected_) return protocol::Result::NotReady;
 if(!protocol::valid(p)) return protocol::Result::InvalidArgument;
 Writer w(scratch_);w.u8(uint8_t(Op::SendBeacon));beacon(w,p);const auto result=request(w) ? protocol::Result(result_) : protocol::Result::HardwareError;
 if(result==protocol::Result::Ok) notifyActivity(protocol::PacketType::Beacon,true,remoteStamp_-offset_,protocol::BeaconPacketSize,0,nullptr,lastTxRestarted_);
 return result;
}
#ifdef SPLIT_DIAGNOSTICS
protocol::Result RemoteProtocol::diagnostic(uint8_t mode) {
 Writer w(scratch_);w.u8(uint8_t(Op::Diagnostic));w.u8(mode);
 return request(w) ? protocol::Result(result_) : protocol::Result::HardwareError;
}
#endif
void RemoteProtocol::dispatch(const Event& e) {
 Reader r(e.bytes,e.length);
 if(e.kind==Kind::Observation) {
  packet_statistics::Observation o;observation(r,o);o.receivedAtMs-=offset_;
  if(r.done()) stats_.observe(o);else ++badFrames;return;
 }
 if(e.kind==Kind::RadioError) { if(r.done()) stats_.recordRadioError();return; }
 radio::PacketInfo i;info(r,i);i.receivedAtMs-=offset_;
 if(e.kind==Kind::Data) {
  protocol::Header h;header(r,h);
  if(!r.ok || !protocol::valid(h,i.length) || r.length-r.at!=i.length) { ++badFrames;return; }
  fragment_.header=h;fragment_.reception=i;fragment_.rawLength=i.length;
  // Classification belongs to the host's shared profile definitions.
  fragment_.profile=i.hasModulation ? modulation_profiles::identify(i.spreadingFactor,i.bandwidth,i.codingRate) : packet_statistics::UnknownProfile;
  memcpy(fragment_.rawBytes,r.data+r.at,i.length);
  notifyActivity(protocol::PacketType::Data,false,i.receivedAtMs,i.length,h.flags,&i,protocol::restartFlag(h.flags));
  if(data_) data_->onFragment(fragment_);
 } else if(e.kind==Kind::Control) {
  protocol::ControlPacket p;control(r,p);if(r.done() && protocol::valid(p,i.length)) { notifyActivity(protocol::PacketType::Control,false,i.receivedAtMs,i.length,p.flags,&i,p.flags==protocol::RestartFlag);if(link_) link_->onControl(p,i); }else ++badFrames;
 } else if(e.kind==Kind::Beacon) {
  protocol::BeaconPacket p;beacon(r,p);if(r.done() && protocol::valid(p,i.length)) { notifyActivity(protocol::PacketType::Beacon,false,i.receivedAtMs,i.length,0,&i,false);if(link_) link_->onBeacon(p,i); }else ++badFrames;
 }
}
void RemoteProtocol::poll() {
 pump();if(!connected_) return;
 dispatching_=true;
 while(eventCount_ && connected_) {
  const uint8_t index=eventHead_;
  dispatch(events_[index]);
  eventHead_=(eventHead_+1)%16;--eventCount_;
 }
 dispatching_=false;
 if(connected_ && uint32_t(io_.now()-lastSync_)>=250) simple(Op::Status);
}
}
