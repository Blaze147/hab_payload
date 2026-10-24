#include "StatisticsManager.h"
float StatisticsManager::ProfileSnapshot::corruptionRate() const {
 return crcCheckedPackets ? float(corruptPackets)/crcCheckedPackets : 0;
}
float StatisticsManager::ProfileSnapshot::provisionalMissingRate() const {
 const uint32_t total=uniqueFragments+pendingMissing;
 return total ? float(pendingMissing)/total : 0;
}
float StatisticsManager::ProfileSnapshot::throughputBytesPerSecond(uint32_t now) const {
 const uint32_t elapsed=now-firstAtMs;
 return observed && elapsed ? float(wireBytes)*1000/elapsed : 0;
}
float StatisticsManager::ProfileSnapshot::goodputBytesPerSecond(uint32_t now) const {
 const uint32_t elapsed=now-firstAtMs;
 return observed && elapsed ? float(uniquePayloadBytes)*1000/elapsed : 0;
}
float StatisticsManager::RecentSnapshot::corruptionRate() const {
 return crcCheckedPackets ? float(corruptPackets)/crcCheckedPackets : 0;
}
float StatisticsManager::RecentSnapshot::throughputBytesPerSecond(uint32_t now) const {
 const uint32_t elapsed=now-firstAtMs;
 return packets && elapsed ? float(wireBytes)*1000/elapsed : 0;
}
float StatisticsManager::RecentSnapshot::goodputBytesPerSecond(uint32_t now) const {
 const uint32_t elapsed=now-firstAtMs;
 return packets && elapsed ? float(uniquePayloadBytes)*1000/elapsed : 0;
}
namespace {
void mergeSummary(StatisticsManager::Summary& to,const StatisticsManager::Summary& from) {
 auto& a=to.packets;const auto& b=from.packets;
 const uint32_t count=a.packets+b.packets;
 if(b.observed) {
  if(!a.observed) { a.firstAtMs=b.firstAtMs;a.minRssi=b.minRssi;a.minSnr=b.minSnr; }
  else { if(b.minRssi<a.minRssi) a.minRssi=b.minRssi;if(b.minSnr<a.minSnr) a.minSnr=b.minSnr; }
  a.lastAtMs=b.lastAtMs;a.lastRssi=b.lastRssi;a.lastSnr=b.lastSnr;a.observed=true;
 }
 if(count) {
  a.meanRssi=(double(a.meanRssi)*a.packets+double(b.meanRssi)*b.packets)/count;
  a.meanSnr=(double(a.meanSnr)*a.packets+double(b.meanSnr)*b.packets)/count;
 }
 a.packets=count;
 a.corruptPackets+=b.corruptPackets;a.malformedPackets+=b.malformedPackets;
 a.crcCheckedPackets+=b.crcCheckedPackets;a.wireBytes+=b.wireBytes;
 a.uniquePayloadBytes+=b.uniquePayloadBytes;a.dataPackets+=b.dataPackets;
 a.uniqueFragments+=b.uniqueFragments;a.duplicateFragments+=b.duplicateFragments;
 a.retryPackets+=b.retryPackets;a.trackingUnavailable+=b.trackingUnavailable;
 to.airtimeMs+=from.airtimeMs;to.wallMs+=from.wallMs;to.dataWindows+=from.dataWindows;
}
}
StatisticsManager::Summary StatisticsManager::ProfileRecord::recent(uint32_t now) const {
 Summary result;
 const uint8_t first=sampleCount==RecentPacketCount ? sampleNext : 0;
 for(uint8_t i=0;i<sampleCount;++i) mergeSummary(result,samples[(first+i)%RecentPacketCount].summary);
 if(sampleCount) result.wallMs=(active ? now : endedAtMs)-samples[first].startedAtMs;
 return result;
}
const StatisticsManager::ProfileRecord* StatisticsManager::profileRecord(uint8_t id,uint8_t age) const {
 if(!dense_ || id>=ProfileCount || age>=2) return nullptr;
 const auto& r=dense_->records[id][age];return r.instanceId ? &r : nullptr;
}
void StatisticsManager::recordReliableGoodput(uint8_t id,float score) {
 if(!dense_ || id>=ProfileCount || dense_->activeProfile!=id) return;
 auto& record=dense_->records[id][0];
 if(score>record.peakAirtimeGoodput) record.peakAirtimeGoodput=score;
}
float StatisticsManager::peakAirtimeGoodput(uint8_t id) const {
 float peak=0;
 for(uint8_t age=0;age<2;++age) {
  if(const auto* record=profileRecord(id,age)) if(record->peakAirtimeGoodput>peak) peak=record->peakAirtimeGoodput;
 }
 return peak;
}
void StatisticsManager::beginDataWindow(uint8_t id,uint32_t start,uint32_t end) {
 if(!dense_ || id>=ProfileCount || int32_t(end-start)<=0) return;
 if(dense_->windowOpen && dense_->activeProfile==id && dense_->windowStartedAt==start && dense_->windowDeadline==end) return;
 if(dense_->windowOpen) endDataWindow(start);
 if(dense_->activeProfile!=id) {
  endProfileInstance(start);
  dense_->records[id][1]=dense_->records[id][0];
  auto& record=dense_->records[id][0];record=ProfileRecord{};
  record.instanceId=++dense_->nextInstanceId;
  if(!record.instanceId) record.instanceId=++dense_->nextInstanceId;
  record.profile=id;record.active=true;record.startedAtMs=start;
  dense_->activeProfile=id;
  // Reset diagnostic accumulation too, while retaining live gap ownership and
  // message identities so duplicates/late fragments survive profile changes.
  const auto pending=profiles_[id].pendingMissing;
  profiles_[id]=ProfileSnapshot{};profiles_[id].pendingMissing=pending;
  // Packet diagnostic rings also start fresh for this visit.
  dense_->count[id]=dense_->next[id]=dense_->airtimeCount[id]=dense_->airtimeNext[id]=0;
 }
 dense_->windowStartedAt=start;dense_->windowDeadline=end;dense_->windowOpen=true;
}
void StatisticsManager::endDataWindow(uint32_t end) {
 if(!dense_ || !dense_->windowOpen) return;
 // Late servicing cannot stretch a completed data window into a control slot.
 if(int32_t(end-dense_->windowDeadline)>0) end=dense_->windowDeadline;
 auto& record=dense_->records[dense_->activeProfile][0];
 ++record.total.dataWindows;record.total.wallMs=end-record.startedAtMs;
 dense_->windowOpen=false;
}
void StatisticsManager::endProfileInstance(uint32_t end) {
 if(!dense_) return;
 endDataWindow(end);
 if(dense_->activeProfile<ProfileCount) {
  auto& record=dense_->records[dense_->activeProfile][0];
  record.active=false;record.endedAtMs=end;record.total.wallMs=end-record.startedAtMs;
 }
 dense_->activeProfile=UnknownProfile;
}
void StatisticsManager::recordObservation(const Observation& o,uint32_t useful,bool duplicate,bool unavailable,bool unique) {
 if(!dense_ || !dense_->windowOpen || o.profile!=dense_->activeProfile ||
    int32_t(o.receivedAtMs-dense_->windowStartedAt)<0 ||
    int32_t(o.receivedAtMs-dense_->windowDeadline)>=0 ||
    (!o.data && o.integrity!=Integrity::Corrupt && o.integrity!=Integrity::Malformed)) return;
 Summary delta;auto& p=delta.packets;
 p.observed=true;p.packets=1;p.wireBytes=o.wireBytes;p.uniquePayloadBytes=useful;
 p.firstAtMs=p.lastAtMs=o.receivedAtMs;p.lastRssi=p.minRssi=o.rssi;
 p.lastSnr=p.minSnr=o.snr;p.meanRssi=o.rssi;p.meanSnr=o.snr;
 p.corruptPackets=o.integrity==Integrity::Corrupt;p.malformedPackets=o.integrity==Integrity::Malformed;
 p.crcCheckedPackets=o.crcChecked || o.integrity==Integrity::Valid || o.integrity==Integrity::Corrupt;
 p.dataPackets=o.data && (o.integrity==Integrity::Valid || o.integrity==Integrity::Unchecked);
 p.retryPackets=p.dataPackets && o.retryCount;p.duplicateFragments=duplicate;
 p.uniqueFragments=unique;p.trackingUnavailable=unavailable;
 // Unknown airtime cannot produce a complete airtime estimate.
 if(!o.airtimeMs) p.trackingUnavailable=1;
 delta.airtimeMs=o.airtimeMs;
 auto& record=dense_->records[dense_->activeProfile][0];
 auto& sample=record.samples[record.sampleNext];
 sample=PacketRecord{};sample.endedAtMs=o.receivedAtMs;
 sample.startedAtMs=o.receivedAtMs-o.airtimeMs;
 if(int32_t(sample.startedAtMs-record.startedAtMs)<0) sample.startedAtMs=record.startedAtMs;
 sample.summary=delta;
 record.sampleNext=(record.sampleNext+1)%RecentPacketCount;
 if(record.sampleCount<RecentPacketCount) ++record.sampleCount;
 mergeSummary(record.total,delta);
}
void StatisticsManager::begin() {
 data_=Snapshot{};
 for(auto& p:profiles_) p=ProfileSnapshot{};
 for(auto& t:trackers_) t=Tracker{};
 historySize_=historyNext_=0;
 if(dense_) {
  for(uint8_t i=0;i<BucketCount;++i) dense_->count[i]=dense_->next[i]=dense_->airtimeCount[i]=dense_->airtimeNext[i]=0;
  for(auto& records:dense_->records) for(auto& record:records) record=ProfileRecord{};
  dense_->activeProfile=UnknownProfile;dense_->nextInstanceId=0;dense_->windowOpen=false;
 }
}
bool StatisticsManager::setLogging(Mode mode,Observation* storage,size_t capacity,DenseStorage* dense) {
 if(mode!=Mode::Minimal && mode!=Mode::Heavy) return false;
 if(mode==Mode::Heavy && ((!dense) ||
    ((storage==nullptr)!=(capacity==0)))) return false;
 mode_=mode; dense_=mode==Mode::Heavy ? dense : nullptr;
 if(dense_) {
  for(uint8_t i=0;i<BucketCount;++i) dense_->count[i]=dense_->next[i]=dense_->airtimeCount[i]=dense_->airtimeNext[i]=0;
  for(auto& records:dense_->records) for(auto& record:records) record=ProfileRecord{};
  dense_->activeProfile=UnknownProfile;dense_->nextInstanceId=0;dense_->windowOpen=false;
 }
 history_=mode==Mode::Heavy ? storage : nullptr;
 historyCapacity_=mode==Mode::Heavy ? capacity : 0;
 historySize_=historyNext_=0; return true;
}
bool StatisticsManager::history(size_t index,Observation& out) const {
 if(index>=historySize_) return false;
 const size_t start=historySize_==historyCapacity_ ? historyNext_ : 0;
 out=history_[(start+index)%historyCapacity_]; return true;
}
bool StatisticsManager::recentPacket(uint8_t id,size_t index,RecentSample& out) const {
 if(!dense_) return false;
 const uint8_t bucket=id<ProfileCount ? id : UnknownProfile;
 if(index>=dense_->count[bucket]) return false;
 const size_t start=dense_->count[bucket]==RecentPacketCount ? dense_->next[bucket] : 0;
 out=dense_->samples[bucket][(start+index)%RecentPacketCount]; return true;
}
bool StatisticsManager::recentProfile(uint8_t id,RecentSnapshot& out) const {
 out=RecentSnapshot{};
 if(!dense_) return false;
 const uint8_t bucket=id<ProfileCount ? id : UnknownProfile;
 out.packets=dense_->count[bucket];
 for(uint8_t i=0;i<out.packets;++i) {
  const uint8_t start=out.packets==RecentPacketCount ? dense_->next[bucket] : 0;
  const auto& sample=dense_->samples[bucket][(start+i)%RecentPacketCount];
  const auto& o=sample.observation;
  if(!i) out.firstAtMs=o.receivedAtMs;
  out.lastAtMs=o.receivedAtMs;
  out.wireBytes+=o.wireBytes; out.uniquePayloadBytes+=sample.uniquePayloadBytes;
  out.meanRssi+=o.rssi; out.meanSnr+=o.snr;
  if(!i || o.rssi<out.minRssi) out.minRssi=o.rssi;
  if(!i || o.snr<out.minSnr) out.minSnr=o.snr;
  if(o.crcChecked || o.integrity==Integrity::Valid || o.integrity==Integrity::Corrupt) ++out.crcCheckedPackets;
  if(o.integrity==Integrity::Corrupt) ++out.corruptPackets;
  if(o.integrity==Integrity::Malformed) ++out.malformedPackets;
  if((o.integrity==Integrity::Valid || o.integrity==Integrity::Unchecked) && o.data && o.retryCount) ++out.retryPackets;
  if(sample.duplicate) ++out.duplicatePackets;
  if(sample.trackingUnavailable) ++out.trackingUnavailable;
 }
 if(out.packets) { out.meanRssi/=out.packets; out.meanSnr/=out.packets; }
 return true;
}
bool StatisticsManager::recentAirtimeProfile(uint8_t id,uint32_t since,AirtimeSnapshot& out) const {
 out=AirtimeSnapshot{}; if(!dense_) return false;
 const uint8_t b=id<ProfileCount ? id : UnknownProfile;
 for(uint8_t i=0;i<dense_->airtimeCount[b];++i) {
  const auto& sample=dense_->airtime[b][i];
  if(int32_t(sample.receivedAtMs-since)<0) continue;
  const uint32_t start=sample.receivedAtMs-sample.airtimeMs;
  if(!out.packets || int32_t(start-out.firstAtMs)<0) out.firstAtMs=start;
  ++out.packets; out.airtimeMs+=sample.airtimeMs;
  out.uniquePayloadBytes+=sample.uniquePayloadBytes; out.meanSnr+=sample.snr;
  if(sample.corrupt) ++out.corruptPackets;
  if(sample.trackingUnavailable) ++out.trackingUnavailable;
 }
 if(out.packets) out.meanSnr/=out.packets;
 return true;
}
StatisticsManager::ProfileSnapshot StatisticsManager::profile(uint8_t id) const {
 if(const auto* record=profileRecord(id)) return record->total.packets;
 return profiles_[id<ProfileCount ? id : UnknownProfile];
}
bool StatisticsManager::seen(const Tracker& t,uint16_t i) const {
 return t.seen[i/8] & (1U<<(i%8));
}
uint8_t StatisticsManager::gap(const Tracker& t,uint16_t i) const {
 return (t.gaps[i/2] >> ((i%2)*4)) & 15;
}
void StatisticsManager::setGap(Tracker& t,uint16_t i,uint8_t owner) {
 const unsigned shift=(i%2)*4;
 t.gaps[i/2]=uint8_t((t.gaps[i/2] & ~(15U<<shift)) | (owner<<shift));
}
void StatisticsManager::clearTracker(Tracker& t) {
 for(uint16_t i=0;i<t.high;++i) {
  const uint8_t owner=gap(t,i);
  if(owner) --profiles_[owner-1].pendingMissing;
 }
 t=Tracker{};
}
bool StatisticsManager::releaseMessage(uint16_t session,uint16_t message) {
 for(auto& t:trackers_) if(t.used && t.session==session && t.message==message) {
  clearTracker(t); return true;
 }
 return false;
}
bool StatisticsManager::pendingFragment(uint16_t session,uint16_t message,uint16_t i) const {
 if(i>=FragmentCapacity) return false;
 for(const auto& t:trackers_) if(t.used && t.session==session && t.message==message)
  return gap(t,i)!=0;
 return false;
}
void StatisticsManager::track(const Observation& o,uint8_t bucket) {
 auto& p=profiles_[bucket]; Tracker* target=nullptr;
 for(auto& t:trackers_) if(t.used && t.session==o.sessionId && t.message==o.messageId) {
  target=&t; break;
 }
 if(!target) {
  for(auto& t:trackers_) if(!t.used) { target=&t; break; }
  if(!target) for(auto& t:trackers_) if(t.received==t.count) { target=&t; clearTracker(t); break; }
  if(!target) { ++p.trackingUnavailable; return; }
  target->used=true; target->session=o.sessionId; target->message=o.messageId; target->count=o.fragmentCount;
 }
 auto& t=*target;
 if(t.count!=o.fragmentCount) { ++p.trackingUnavailable; return; }
 if(seen(t,o.fragmentIndex)) { ++p.duplicateFragments; return; }
 // Only indices actually passed by a valid arrival become provisional gaps.
 for(uint16_t i=t.high;i<o.fragmentIndex;++i) {
  setGap(t,i,bucket+1); ++p.pendingMissing;
 }
 if(o.fragmentIndex>=t.high) t.high=o.fragmentIndex+1;
 const uint8_t owner=gap(t,o.fragmentIndex);
 if(owner) { --profiles_[owner-1].pendingMissing; setGap(t,o.fragmentIndex,0); }
 t.seen[o.fragmentIndex/8] |= uint8_t(1U<<(o.fragmentIndex%8));
 ++t.received; ++p.uniqueFragments; p.uniquePayloadBytes+=o.payloadBytes;
}
void StatisticsManager::observe(const Observation& o) {
 const uint8_t bucket=o.profile<ProfileCount ? o.profile : UnknownProfile;
 auto& p=profiles_[bucket];
 const uint32_t payloadBefore=p.uniquePayloadBytes, duplicatesBefore=p.duplicateFragments;
 const uint32_t unavailableBefore=p.trackingUnavailable;
 ++data_.receivedPackets; data_.hasReceiveMeasurement=true;
 data_.lastPacketLength=o.wireBytes; data_.lastRssi=o.rssi; data_.lastSnr=o.snr;
 if(!p.observed) {
  p.firstAtMs=o.receivedAtMs; p.observed=true;
  p.minRssi=o.rssi; p.minSnr=o.snr;
 } else {
  if(o.rssi<p.minRssi) p.minRssi=o.rssi;
  if(o.snr<p.minSnr) p.minSnr=o.snr;
 }
 p.lastAtMs=o.receivedAtMs; ++p.packets; p.wireBytes+=o.wireBytes;
 p.lastRssi=o.rssi; p.lastSnr=o.snr;
 p.meanRssi+=(float(o.rssi)-p.meanRssi)/p.packets;
 p.meanSnr+=(o.snr-p.meanSnr)/p.packets;
 if(o.crcChecked || o.integrity==Integrity::Valid || o.integrity==Integrity::Corrupt) ++p.crcCheckedPackets;
 if(o.integrity==Integrity::Corrupt) { ++p.corruptPackets; ++data_.rejectedPackets; }
 if(o.integrity==Integrity::Malformed) { ++p.malformedPackets; ++data_.rejectedPackets; }
 if((o.integrity==Integrity::Valid || o.integrity==Integrity::Unchecked) && o.data) {
  ++p.dataPackets;
  if(o.retryCount) ++p.retryPackets;
  if(o.fragmentCount && o.fragmentCount<=FragmentCapacity && o.fragmentIndex<o.fragmentCount)
   track(o,bucket);
  else ++p.trackingUnavailable;
 }
 recordObservation(o,p.uniquePayloadBytes-payloadBefore,p.duplicateFragments!=duplicatesBefore,
                   p.trackingUnavailable!=unavailableBefore,p.uniquePayloadBytes!=payloadBefore);
 if(dense_) {
  // Valid control/beacon traffic cannot displace data samples. Corrupt or
  // malformed observations contribute cost even when packet type is unknown.
  if(o.airtimeMs && (o.data || o.integrity==Integrity::Corrupt || o.integrity==Integrity::Malformed)) {
   auto& a=dense_->airtime[bucket][dense_->airtimeNext[bucket]];
   a.receivedAtMs=o.receivedAtMs; a.airtimeMs=o.airtimeMs; a.snr=o.snr;
   a.uniquePayloadBytes=uint16_t(p.uniquePayloadBytes-payloadBefore);
   a.corrupt=o.integrity==Integrity::Corrupt || o.integrity==Integrity::Malformed;
   a.trackingUnavailable=p.trackingUnavailable!=unavailableBefore;
   dense_->airtimeNext[bucket]=(dense_->airtimeNext[bucket]+1)%AirtimeSampleCount;
   if(dense_->airtimeCount[bucket]<AirtimeSampleCount) ++dense_->airtimeCount[bucket];
  }
  auto& sample=dense_->samples[bucket][dense_->next[bucket]];
  sample.observation=o;
  sample.uniquePayloadBytes=uint16_t(p.uniquePayloadBytes-payloadBefore);
  sample.duplicate=p.duplicateFragments!=duplicatesBefore;
  sample.trackingUnavailable=p.trackingUnavailable!=unavailableBefore;
  dense_->next[bucket]=(dense_->next[bucket]+1)%RecentPacketCount;
  if(dense_->count[bucket]<RecentPacketCount) ++dense_->count[bucket];
 }
 if(mode_==Mode::Heavy && history_) {
  history_[historyNext_]=o; historyNext_=(historyNext_+1)%historyCapacity_;
  if(historySize_<historyCapacity_) ++historySize_;
 }
}
void StatisticsManager::recordPacketRejected() { ++data_.rejectedPackets; }
void StatisticsManager::recordMessageCompleted() { ++data_.completedMessages; }
void StatisticsManager::recordMessageRejected() { ++data_.rejectedMessages; }
void StatisticsManager::recordRadioError() { ++data_.radioErrors; }
