#include "LinkManager.h"
namespace {
bool due(uint32_t now,uint32_t at) { return int32_t(now-at)>=0; }
bool validProfile(uint8_t id) { return id<=uint8_t(link_profiles::Id::P4); }
uint8_t flags(protocol::ControlFlag f) { return uint8_t(f); }
}
void LinkManager::begin() { protocol_.setLinkReceiver(this); stop(); }
void LinkManager::stop() {
 statistics_.endProfileInstance(lastPollMs_);
 discardPending();
 phase_=Phase::Stopped; conversation_=Conversation::Idle;
 pending_=sending_=established_=false; probing_=probeNegotiating_=false; protocol_.allowDataTransmission(false);
}
void LinkManager::fail(protocol::Result result) {
 statistics_.endProfileInstance(lastPollMs_);
 discardPending();
 pending_=sending_=established_=false; probing_=probeNegotiating_=false; phase_=Phase::Fault;
 lastResult_=result; protocol_.allowDataTransmission(false);
}
protocol::Result LinkManager::start(Role role,const Config& config,uint32_t now) {
 if((role!=Role::Balloon && role!=Role::Ground) || !config.dataWindowMs ||
    config.dataWindowMs>120000UL || !config.controlWindowSeconds || config.controlWindowSeconds>120 ||
    config.quietMs>=uint32_t(config.controlWindowSeconds)*1000 || !config.replyTimeoutMs ||
    !config.degradedWindowsBeforeFallback || !config.silentDataWindowsBeforeFallback || !config.maximumProbeNegotiationWindows || !config.minimumProbePackets || config.minimumProbePackets>StatisticsManager::RecentPacketCount ||
    !config.upwardProbeIntervalMs || config.upwardProbeIntervalMs>0x7fffffffUL || config.maximumCorruptionRate<0 || config.maximumCorruptionRate>1 ||
    !config.strongWindowsForUpgrade || config.nearPeakFraction<=0 || config.nearPeakFraction>1 ||
    !config.evidenceRetryCooldownMs || config.evidenceRetryCooldownMs>0x7fffffffUL ||
    config.evidenceGoodputImprovement<=0 || config.evidenceSnrImprovementDb<=0 ||
    config.minimumGoodputImprovement<0 || config.maximumGoodputDrop<0 || config.maximumGoodputDrop>1) return protocol::Result::InvalidArgument;
 if(protocol_.radioState()==radio::State::Transmitting) return protocol::Result::Busy;
 if(protocol_.radioState()==radio::State::Uninitialized) return protocol::Result::NotReady;
 statistics_.endProfileInstance(now); lastPollMs_=now;
 discardPending(); config_=config; role_=role; dataProfile_=desiredProfile_=bestProfile_=link_profiles::Id::P0;
 acquisition_=nextBeacon_=initialProbe_=true; established_=pending_=heard_=sending_=committed_=probing_=false;
 lastRetransmitAccepted_=lastRetransmitRefused_=false;
 probeNegotiating_=false; failedProbeWindows_=0; probeTarget_=probeFrom_=link_profiles::Id::P0;
 probeReason_=ProbeReason::None; silentDataWindows_=degradedWindows_=0; silenceRecovery_=false;
 degradationTarget_=StatisticsManager::UnknownProfile; degradationInstanceId_=0;
 for(auto& history:profileHistory_) history=ProfileHistory{};
 nextProbeAt_=now; profileSinceMs_=now;
 automatic_=config.autoProbe; conversationId_=missed_=0; bestGoodput_=0;
 trialPackets_=0; nextSlideMs_=0; request_=protocol::ControlPacket{}; reply_=protocol::ControlPacket{};
 conversation_=Conversation::Idle; lastResult_=protocol::Result::Ok;
 if(setProfile(link_profiles::Id::P0)!=radio::Status::Ok) { fail(protocol::Result::HardwareError); return lastResult_; }
 if(role_==Role::Balloon) enterData(now);
 else {
  phase_=Phase::Searching; protocol_.allowDataTransmission(false);
  if(protocol_.receive()!=radio::Status::Ok) fail(protocol::Result::HardwareError);
 }
 return lastResult_;
}
radio::Status LinkManager::setProfile(link_profiles::Id id) {
 const link_profiles::Definition* selected=nullptr;
 switch(id) {
 case link_profiles::Id::P0: selected=&link_profiles::P0; break;
 case link_profiles::Id::P1: selected=&link_profiles::P1; break;
 case link_profiles::Id::P2: selected=&link_profiles::P2; break;
 case link_profiles::Id::P3: selected=&link_profiles::P3; break;
 case link_profiles::Id::P4: selected=&link_profiles::P4; break;
 default: return radio::Status::InvalidConfig;
 }
 const auto state=protocol_.radioState();
 if(state==radio::State::Uninitialized) return radio::Status::NotInitialized;
 if(state==radio::State::Fault) return radio::Status::HardwareError;
 if(state==radio::State::Transmitting) return radio::Status::Busy;
 auto config=protocol_.radioConfig();
 // Reapplying an identical profile would stop RX and discard an in-flight frame.
 if(config.spreadingFactor==selected->spreadingFactor && config.bandwidth==selected->bandwidth &&
    config.codingRate==selected->codingRate) return radio::Status::Ok;
 config.spreadingFactor=selected->spreadingFactor; config.bandwidth=selected->bandwidth; config.codingRate=selected->codingRate;
 return protocol_.configureRadio(config);
}
bool LinkManager::p0(const radio::PacketInfo& info) const {
 return info.hasModulation && link_profiles::identify(info.spreadingFactor,info.bandwidth,info.codingRate)==0;
}
bool LinkManager::withinWindow(uint32_t now) const {
 return phase_==Phase::Control && due(now,phaseStart_+config_.quietMs) && !due(now,phaseEnd_);
}
bool LinkManager::busyConversation() const {
 return phase_==Phase::Control && conversation_!=Conversation::Idle && conversation_!=Conversation::Exhausted;
}
protocol::Result LinkManager::requestDataProfile(link_profiles::Id id) {
 if(role_!=Role::Ground || !validProfile(uint8_t(id))) return protocol::Result::InvalidArgument;
 if(busyConversation()) return protocol::Result::Busy;
 desiredProfile_=id; automatic_=false; probing_=probeNegotiating_=false; failedProbeWindows_=0; probeReason_=ProbeReason::None;
 degradationTarget_=StatisticsManager::UnknownProfile;degradedWindows_=0; return protocol::Result::Ok;
}
protocol::Result LinkManager::requestRetransmission(uint16_t message,uint16_t groups,const uint16_t* maps,uint8_t count) {
 if(role_!=Role::Ground || count!=protocol::controlBitmapCount(groups) || (count && !maps))
  return protocol::Result::InvalidArgument;
 if(busyConversation()) return protocol::Result::Busy;
 request_.retransmitMessageId=message; request_.retransmitGroupBitmap=groups; request_.retransmitBitmapCount=count;
 for(uint8_t i=0;i<count;++i) request_.retransmitBitmaps[i]=maps[i];
 return protocol::Result::Ok;
}
protocol::Result LinkManager::sendControl(const protocol::ControlPacket& p,uint32_t now) { return protocol_.sendControl(p,now); }
protocol::Result LinkManager::sendBeacon(const protocol::BeaconPacket& p,uint32_t now) { return protocol_.sendBeacon(p,now); }
void LinkManager::baseline() {
 const auto p=statistics_.profile(uint8_t(dataProfile_));
 baseline_.packets=p.dataPackets; baseline_.unavailable=p.trackingUnavailable;
}
void LinkManager::enterData(uint32_t start) {
 if(degradationTarget_<StatisticsManager::ProfileCount && uint8_t(dataProfile_)<=degradationTarget_) {
  degradationTarget_=StatisticsManager::UnknownProfile;degradedWindows_=0;
 }
 phase_=Phase::Data; phaseStart_=start;
 int32_t duration=int32_t(config_.dataWindowMs)+(role_==Role::Ground ? nextSlideMs_ : 0);
 if(duration<1) duration=1;
 phaseEnd_=start+uint32_t(duration); nextSlideMs_=0;
 statistics_.beginDataWindow(uint8_t(dataProfile_),start,phaseEnd_);
 conversation_=Conversation::Idle; conversationId_=0; pending_=sending_=false;
 if(setProfile(dataProfile_)!=radio::Status::Ok) { fail(protocol::Result::HardwareError); return; }
 if(role_==Role::Balloon) protocol_.setDataWindow(phaseEnd_);
 else protocol_.allowDataTransmission(false);
 const auto status=role_==Role::Ground ? protocol_.receive() : protocol_.idle();
 if(status!=radio::Status::Ok) { fail(protocol::Result::HardwareError); return; }
 baseline();
}
void LinkManager::recordFailedUpgrade(uint32_t now) {
 auto& history=profileHistory_[uint8_t(probeFrom_)];
 history.failedUpgrade=true; history.retryAt=now+config_.evidenceRetryCooldownMs;
 history.strongWindows=0;
}
void LinkManager::chooseProfile(uint32_t now) {
 if(role_!=Role::Ground || !automatic_) return;
 if(!established_) return;
 const auto p=statistics_.profile(uint8_t(dataProfile_));
 const uint32_t packets=p.dataPackets-baseline_.packets;
 desiredProfile_=dataProfile_;
 auto& history=profileHistory_[uint8_t(dataProfile_)];
 const auto* record=statistics_.profileRecord(uint8_t(dataProfile_));
 if(!record || record->instanceId!=degradationInstanceId_) {
  degradedWindows_=0;degradationInstanceId_=record ? record->instanceId : 0;
 }
 if(packets) silentDataWindows_=0;
 else {
  history.strongWindows=0;degradedWindows_=0;
  if(silentDataWindows_<255) ++silentDataWindows_;
 }
 if(silentDataWindows_>=config_.silentDataWindowsBeforeFallback || silenceRecovery_) {
  if(probing_) recordFailedUpgrade(now);
  probing_=probeNegotiating_=initialProbe_=false; silenceRecovery_=true;
  degradationTarget_=StatisticsManager::UnknownProfile;degradedWindows_=0;
  desiredProfile_=link_profiles::Id::P0;
  nextProbeAt_=now+config_.upwardProbeIntervalMs;
  return;
 }
 if(degradationTarget_<StatisticsManager::ProfileCount) {
  desiredProfile_=static_cast<link_profiles::Id>(degradationTarget_);return;
 }
 // Negotiation survives isolated silent windows; the old profile is not scored
 // as the candidate. Silence recovery takes precedence at the configured limit.
 if(probing_ && probeNegotiating_) { degradedWindows_=0;desiredProfile_=probeTarget_; return; }
 if(!packets) return; // Hold the current profile for the first two silent windows.
 if(p.trackingUnavailable!=baseline_.unavailable) { history.strongWindows=0;degradedWindows_=0; return; } // Incomplete goodput cannot justify a probe.
 trialPackets_+=packets;
 if(trialPackets_<config_.minimumProbePackets && packets) { degradedWindows_=0;return; }
 if(!record) return;
 const auto recent=record->recent(now);
 // Require fresh data in this window; historical samples cannot hide silence.
 if(packets && recent.packets.packets<config_.minimumProbePackets) { degradedWindows_=0;return; }
 if(recent.packets.trackingUnavailable) { history.strongWindows=0;degradedWindows_=0; return; }
 const float score=recent.airtimeGoodputBytesPerSecond();
 const bool reliable=packets>0 && recent.packets.packets>=config_.minimumProbePackets && score>0 &&
  float(recent.packets.corruptPackets)/recent.packets.packets<=config_.maximumCorruptionRate;
 if(!reliable) {
  history.strongWindows=0;degradedWindows_=0;
  if(probing_) recordFailedUpgrade(now);
  if(uint8_t(dataProfile_)) desiredProfile_=probing_ ? bestProfile_ :
    static_cast<link_profiles::Id>(uint8_t(dataProfile_)-1);
  initialProbe_=probing_=false; nextProbeAt_=now+config_.upwardProbeIntervalMs; return;
 }
 statistics_.recordReliableGoodput(uint8_t(dataProfile_),score);
 if(probing_ && score<=bestGoodput_*(1+config_.minimumGoodputImprovement)) {
  recordFailedUpgrade(now);
  desiredProfile_=bestProfile_; probing_=initialProbe_=false;
  nextProbeAt_=now+config_.upwardProbeIntervalMs; return;
 }
 // Compare this instance's recent delivery against its cumulative delivery,
 // not the preceding short-term score. Require history outside the recent ring.
 const auto whole=record->whole(now);
 const float reference=whole.airtimeGoodputBytesPerSecond();
 const bool mature=whole.packets.dataPackets>=uint32_t(config_.minimumProbePackets)*2 &&
  whole.airtimeMs>recent.airtimeMs && !whole.packets.trackingUnavailable;
 if(!probing_ && uint8_t(dataProfile_) && mature && reference>0 &&
    score<reference*(1-config_.maximumGoodputDrop)) {
  history.strongWindows=0;
  if(degradedWindows_<255) ++degradedWindows_;
  if(degradedWindows_>=config_.degradedWindowsBeforeFallback) {
   degradationTarget_=uint8_t(dataProfile_)-1;
   desiredProfile_=static_cast<link_profiles::Id>(degradationTarget_);
   initialProbe_=false;nextProbeAt_=now+config_.upwardProbeIntervalMs;
  }
  // Pending degradation takes priority over new upward exploration.
  return;
 }
 degradedWindows_=0;
 const bool continueProbe=probing_ && probeReason_!=ProbeReason::Evidence;
 if(probing_) profileHistory_[uint8_t(probeFrom_)].failedUpgrade=false;
 if(score>=statistics_.peakAirtimeGoodput(uint8_t(dataProfile_))*config_.nearPeakFraction) {
  if(history.strongWindows<255) ++history.strongWindows;
 } else history.strongWindows=0;
 const bool changed=!history.failedUpgrade || (due(now,history.retryAt) &&
  (score>=history.attemptedGoodput*(1+config_.evidenceGoodputImprovement) ||
   recent.packets.meanSnr>=history.attemptedSnr+config_.evidenceSnrImprovementDb));
 const bool evidence=history.strongWindows>=config_.strongWindowsForUpgrade && changed;
 const ProbeReason continuingReason=probeReason_;
 bestGoodput_=score; bestProfile_=dataProfile_; probing_=false;
 if((initialProbe_ || continueProbe || due(now,nextProbeAt_) || evidence) && uint8_t(dataProfile_)<4) {
  desiredProfile_=static_cast<link_profiles::Id>(uint8_t(dataProfile_)+1); probing_=true;
  probeReason_=initialProbe_ ? ProbeReason::Startup : (continueProbe ? continuingReason :
   (due(now,nextProbeAt_) ? ProbeReason::Periodic : ProbeReason::Evidence));
  probeFrom_=dataProfile_; history.attemptedGoodput=score; history.attemptedSnr=recent.packets.meanSnr;
  history.strongWindows=0;
  probeTarget_=desiredProfile_; probeNegotiating_=true; failedProbeWindows_=0;
  nextProbeAt_=now+config_.upwardProbeIntervalMs;
 } else if(uint8_t(dataProfile_)==4) {
  initialProbe_=false; nextProbeAt_=now+config_.upwardProbeIntervalMs;
 }
}
void LinkManager::enterWindow(uint32_t start) {
 statistics_.endDataWindow(start);
 chooseProfile(start);
 phaseStart_=start; phaseEnd_=start+uint32_t(config_.controlWindowSeconds)*1000;
 phase_=role_==Role::Balloon && acquisition_ && nextBeacon_ ? Phase::Beacon : Phase::Control;
 protocol_.allowDataTransmission(false); conversation_=Conversation::Idle;
 conversationId_=0; pending_=heard_=sending_=committed_=false;
 actionAt_=start+config_.quietMs;
 if(setProfile(link_profiles::Id::P0)!=radio::Status::Ok || protocol_.receive()!=radio::Status::Ok)
  fail(protocol::Result::HardwareError);
}
void LinkManager::finishWindow(uint32_t boundary) {
 if(phase_==Phase::Beacon) nextBeacon_=false;
 else {
  if(heard_) missed_=0; else if(missed_<255) ++missed_;
  if(missed_>=2) {
   if(dataProfile_!=link_profiles::Id::P0) trialPackets_=0;
   dataProfile_=desiredProfile_=link_profiles::Id::P0; profileSinceMs_=boundary; silentDataWindows_=0;
  }
  if(missed_>=4) { acquisition_=true; established_=false; nextBeacon_=true; }
  else if(acquisition_) nextBeacon_=true;
 }
 if(role_==Role::Ground && probing_ && probeNegotiating_) {
  // Count whole control windows, not individual failed handshakes.
  if(++failedProbeWindows_>=config_.maximumProbeNegotiationWindows) {
   recordFailedUpgrade(boundary);
   probing_=probeNegotiating_=initialProbe_=false;
   desiredProfile_=dataProfile_; nextProbeAt_=boundary+config_.upwardProbeIntervalMs;
  } else desiredProfile_=probeTarget_;
 }
 // Pending changes are discarded, but application messages and request storage survive.
 discardPending();
 if(!committed_) nextSlideMs_=0;
 pending_=false; sending_=false; conversationId_=0;
 if(role_==Role::Ground && missed_>=4) {
  statistics_.endProfileInstance(boundary);
  phase_=Phase::Searching; conversation_=Conversation::Idle; protocol_.allowDataTransmission(false);
  if(setProfile(link_profiles::Id::P0)!=radio::Status::Ok || protocol_.receive()!=radio::Status::Ok)
   fail(protocol::Result::HardwareError);
 } else enterData(boundary);
}
bool LinkManager::sameRequest(const protocol::ControlPacket& a,const protocol::ControlPacket& b) const {
 if(a.profileChangeRequest!=b.profileChangeRequest || a.retransmitMessageId!=b.retransmitMessageId ||
    a.retransmitGroupBitmap!=b.retransmitGroupBitmap || a.retransmitBitmapCount!=b.retransmitBitmapCount) return false;
 for(uint8_t i=0;i<a.retransmitBitmapCount;++i) if(a.retransmitBitmaps[i]!=b.retransmitBitmaps[i]) return false;
 return true;
}
void LinkManager::discardPending() {
 if(pending_ && role_==Role::Balloon && retransmissionAccepted_ && retransmission_)
  retransmission_->discard(request_);
 pending_=false;
}
void LinkManager::abortAttempt(uint32_t now) {
 discardPending();
 pending_=false; committed_=false; sending_=false; nextSlideMs_=0;
 conversation_=Conversation::Idle; actionAt_=now+config_.turnaroundMs;
 if(role_==Role::Balloon && protocol_.receive()!=radio::Status::Ok) fail(protocol::Result::HardwareError);
}
void LinkManager::startAttempt(uint32_t now) {
 const uint32_t airtime=protocol_.airtimeMs(protocol::controlPacketSize(request_.retransmitBitmapCount));
 // Keep retrying while another SYN fits. A late attempt may not finish the
 // exchange; existing per-frame/window checks prevent a boundary overrun.
 if(uint32_t(phaseEnd_-now)<=airtime) {
  pending_=false; conversation_=Conversation::Exhausted; return;
 }
 if(conversationId_==255) { conversation_=Conversation::Exhausted; return; }
 ++conversationId_; request_.conversationId=conversationId_;
 request_.flags=flags(protocol::ControlFlag::Syn); request_.profileChangeRequest=uint8_t(desiredProfile_);
 request_.windowTimingSlide=0;
 pending_=true; committed_=false; conversation_=Conversation::SendSyn; actionAt_=now;
}
void LinkManager::commit(bool provisional) {
 if(committed_) {
  conversation_=provisional ? Conversation::Provisional : Conversation::Complete; return;
 }
 committed_=true; pending_=false;
 lastRetransmitAccepted_=request_.retransmitGroupBitmap && reply_.retransmitGroupBitmap==request_.retransmitGroupBitmap;
 lastRetransmitRefused_=request_.retransmitGroupBitmap && !reply_.retransmitGroupBitmap;
 if(dataProfile_!=static_cast<link_profiles::Id>(reply_.profileChangeRequest)) {
  trialPackets_=0; silentDataWindows_=degradedWindows_=0;
 }
 if(dataProfile_!=static_cast<link_profiles::Id>(reply_.profileChangeRequest)) profileSinceMs_=phaseEnd_;
 dataProfile_=static_cast<link_profiles::Id>(reply_.profileChangeRequest);
 if(dataProfile_==link_profiles::Id::P0) silenceRecovery_=false;
 if(role_==Role::Ground && probing_ && probeNegotiating_ && dataProfile_==probeTarget_) {
  probeNegotiating_=false; failedProbeWindows_=0;
 }
 if(role_==Role::Balloon && retransmissionAccepted_ && retransmission_) retransmission_->retransmit(request_);
 established_=true; acquisition_=false;
 conversation_=provisional ? Conversation::Provisional : Conversation::Complete;
}
void LinkManager::poll(uint32_t now) {
 lastPollMs_=now;
 if(phase_==Phase::Stopped || phase_==Phase::Fault) return;
 if(protocol_.radioState()==radio::State::Fault) { fail(protocol::Result::HardwareError); return; }
 if(protocol_.radioState()==radio::State::Transmitting) return;
 if(sending_) {
  sending_=false;
  if(protocol_.transmission()!=radio::TxStatus::Complete) { fail(protocol::Result::HardwareError); return; }
  if(phase_==Phase::Beacon) actionAt_=now+config_.beaconWaitMs;
  else if(conversation_==Conversation::SendSyn) { conversation_=Conversation::WaitSynAck; replyAt_=now+config_.replyTimeoutMs; }
  else if(conversation_==Conversation::SendSynAck) { conversation_=Conversation::WaitAck; replyAt_=now+config_.replyTimeoutMs; }
  else if(conversation_==Conversation::SendAck) {
   if(!due(now,phaseEnd_)) commit(true); else { pending_=false; nextSlideMs_=0; }
  }
  if(protocol_.receive()!=radio::Status::Ok) { fail(protocol::Result::HardwareError); return; }
 }
 if(phase_==Phase::Searching) return;
 if(due(now,phaseEnd_)) {
  const uint32_t boundary=phaseEnd_;
  if(phase_==Phase::Data) enterWindow(boundary); else finishWindow(boundary);
  return;
 }
 if(phase_==Phase::Data) return;
 if(phase_==Phase::Beacon) {
  if(!due(now,actionAt_)) return;
  const uint32_t airtime=protocol_.airtimeMs(protocol::BeaconPacketSize);
  if(uint32_t(phaseEnd_-now)<=airtime) return;
  protocol::BeaconPacket b;
  // Countdown refers to estimated RX completion and the NEXT listening slot.
  b.timeToNextConversation=uint16_t((phaseEnd_+config_.dataWindowMs-now-airtime+99)/100);
  b.conversationDuration=config_.controlWindowSeconds;
  lastResult_=sendBeacon(b,now); if(lastResult_==protocol::Result::Ok) sending_=true;
  else if(lastResult_!=protocol::Result::Busy) fail(lastResult_);
  return;
 }
 if(!withinWindow(now)) return;
 if((conversation_==Conversation::WaitSynAck || conversation_==Conversation::WaitAck) && due(now,replyAt_))
  abortAttempt(now);
 if(role_==Role::Ground && conversation_==Conversation::Idle && due(now,actionAt_)) startAttempt(now);
 const protocol::ControlPacket* packet=nullptr;
 if(conversation_==Conversation::SendSyn) packet=&request_;
 else if(conversation_==Conversation::SendSynAck || conversation_==Conversation::SendAck) packet=&reply_;
 if(!packet || sending_ || !due(now,actionAt_)) return;
 const uint32_t airtime=protocol_.airtimeMs(protocol::controlPacketSize(packet->retransmitBitmapCount));
 if(uint32_t(phaseEnd_-now)<=airtime) { discardPending(); nextSlideMs_=0; conversation_=Conversation::Exhausted; return; }
 lastResult_=sendControl(*packet,now);
 if(lastResult_==protocol::Result::Ok) sending_=true;
 else if(lastResult_!=protocol::Result::Busy) fail(lastResult_);
}
protocol::Result LinkManager::onControl(const protocol::ControlPacket& packet,const radio::PacketInfo& info) {
 if(phase_==Phase::Stopped) return protocol::Result::NotReady;
 if(!protocol::valid(packet,info.length) || !p0(info) || !withinWindow(info.receivedAtMs) || !packet.conversationId ||
    !validProfile(packet.profileChangeRequest)) return protocol::Result::InvalidArgument;
 const uint32_t now=info.receivedAtMs;
 if((conversation_==Conversation::WaitSynAck || conversation_==Conversation::WaitAck) && due(now,replyAt_))
  abortAttempt(now);
 if(role_==Role::Balloon && packet.flags==flags(protocol::ControlFlag::Syn)) {
  if(packet.windowTimingSlide || packet.conversationId<conversationId_) return protocol::Result::InvalidArgument;
  if(packet.conversationId==conversationId_) {
   if(!sameRequest(packet,request_) || !pending_) return protocol::Result::InvalidArgument;
   if(!sending_) { conversation_=Conversation::SendSynAck; actionAt_=now+config_.turnaroundMs; }
   return protocol::Result::Ok;
  }
  discardPending();
  const bool first=!heard_; heard_=true; pending_=true; committed_=false;
  conversationId_=packet.conversationId; request_=packet; reply_=packet;
  reply_.flags=flags(protocol::ControlFlag::SynAck);
  retransmissionAccepted_=packet.retransmitGroupBitmap && retransmission_ && retransmission_->canRetransmit(packet);
  if(packet.retransmitGroupBitmap && !retransmissionAccepted_) {
   reply_.retransmitGroupBitmap=0; reply_.retransmitBitmapCount=0;
  }
  // Positive delays ground; negative advances it. Approximate RF start from RX end.
  const uint32_t airtime=protocol_.airtimeMs(info.length);
  const int32_t arrival=int32_t(now-phaseStart_)-int32_t(airtime);
  int32_t slide=(int32_t(config_.quietMs)-arrival)/10;
  if(first && packet.conversationId>1 && arrival<=config_.earlyRetryThresholdMs)
   slide=int32_t(config_.earlyRetryDelayMs)/10;
  if(slide>127) slide=127;
  if(slide < -128) slide=-128;
  reply_.windowTimingSlide=int8_t(slide);
  conversation_=Conversation::SendSynAck; actionAt_=now+config_.turnaroundMs;
  return protocol::Result::Ok;
 }
 if(role_==Role::Balloon && packet.flags==flags(protocol::ControlFlag::Ack)) {
  if(packet.conversationId!=conversationId_ || packet.windowTimingSlide || !sameRequest(packet,reply_))
   return protocol::Result::InvalidArgument;
  if(conversation_==Conversation::Complete) return protocol::Result::Ok; // Duplicate ACK, no repeated effects.
  if(conversation_!=Conversation::WaitAck || !pending_) return protocol::Result::InvalidArgument;
  heard_=true; commit(false); return protocol::Result::Ok;
 }
 if(role_==Role::Ground && packet.flags==flags(protocol::ControlFlag::SynAck)) {
  if(packet.conversationId!=conversationId_ || packet.profileChangeRequest!=request_.profileChangeRequest ||
     packet.retransmitMessageId!=request_.retransmitMessageId) return protocol::Result::InvalidArgument;
  if(packet.retransmitGroupBitmap && !sameRequest(packet,request_)) return protocol::Result::InvalidArgument;
  if(conversation_==Conversation::Provisional) {
   // Repeat ACK without applying slide/profile again.
   if(!sameRequest(packet,reply_)) return protocol::Result::InvalidArgument;
  } else if(conversation_!=Conversation::WaitSynAck || !pending_) return protocol::Result::InvalidArgument;
  heard_=true; reply_=packet; reply_.flags=flags(protocol::ControlFlag::Ack);
  // Save slide separately; ACK travels ground -> balloon and carries zero slide.
  if(!committed_) nextSlideMs_=protocol::windowTimingSlideMs(packet.windowTimingSlide);
  reply_.windowTimingSlide=0;
  conversation_=Conversation::SendAck; actionAt_=now+config_.turnaroundMs;
  return protocol::Result::Ok;
 }
 return protocol::Result::InvalidArgument;
}
protocol::Result LinkManager::onBeacon(const protocol::BeaconPacket& packet,const radio::PacketInfo& info) {
 if(phase_==Phase::Stopped) return protocol::Result::NotReady;
 if(role_!=Role::Ground || phase_==Phase::Fault || !protocol::valid(packet,info.length) || !p0(info))
  return protocol::Result::InvalidArgument;
 if(!packet.timeToNextConversation) return protocol::Result::InvalidArgument;
 // A valid P0 beacon means the balloon is acquiring (possibly after reboot).
 // Its announced listening window supersedes any old agreement or SYN attempt.
 statistics_.endDataWindow(info.receivedAtMs);
 statistics_.endProfileInstance(info.receivedAtMs);
 discardPending();
 established_=heard_=committed_=false;
 probing_=probeNegotiating_=silenceRecovery_=false;
 probeReason_=ProbeReason::None; failedProbeWindows_=0;
 silentDataWindows_=degradedWindows_=missed_=0;
 degradationTarget_=StatisticsManager::UnknownProfile; degradationInstanceId_=0;
 dataProfile_=link_profiles::Id::P0;
 if(automatic_) desiredProfile_=link_profiles::Id::P0;
 initialProbe_=true; trialPackets_=0; bestGoodput_=0;bestProfile_=link_profiles::Id::P0;
 profileSinceMs_=nextProbeAt_=info.receivedAtMs;
 conversationId_=0;nextSlideMs_=0;
 lastRetransmitAccepted_=lastRetransmitRefused_=false;
 config_.controlWindowSeconds=packet.conversationDuration;
 phase_=Phase::Data; phaseStart_=info.receivedAtMs;
 phaseEnd_=info.receivedAtMs+protocol::beaconCountdownMs(packet.timeToNextConversation);
 statistics_.beginDataWindow(uint8_t(dataProfile_),phaseStart_,phaseEnd_);
 acquisition_=true; pending_=sending_=false; conversation_=Conversation::Idle;
 protocol_.allowDataTransmission(false);
 if(setProfile(link_profiles::Id::P0)!=radio::Status::Ok || protocol_.receive()!=radio::Status::Ok) {
  fail(protocol::Result::HardwareError);return lastResult_;
 }
 baseline(); return protocol::Result::Ok;
}
