#pragma once
#include <RadioManager.h>
#include <vector>
#include <array>
#include <algorithm>
namespace simulation {
using namespace radio;
class FakeRadioAdapter;
struct Channel {
 uint32_t now=0;
 bool drop=false;
 int16_t rssi=-90; float snr=7.5;
 std::vector<FakeRadioAdapter*> radios;
 void advance(uint32_t ms);
};
inline bool compatible(const Config& a,const Config& b) {
 return a.frequency==b.frequency && a.bandwidth==b.bandwidth && a.spreadingFactor==b.spreadingFactor && a.codingRate==b.codingRate && a.preamble==b.preamble && a.crc==b.crc && a.syncWord==b.syncWord;
}
class FakeRadioAdapter : public Adapter {
public:
 explicit FakeRadioAdapter(Channel& c): channel(c) { c.radios.push_back(this); }
 ~FakeRadioAdapter() override { auto& r=channel.radios; r.erase(std::remove(r.begin(),r.end(),this),r.end()); }
 FakeRadioAdapter(const FakeRadioAdapter&)=delete;
 FakeRadioAdapter& operator=(const FakeRadioAdapter&)=delete;
 Channel& channel;
 Config settings;
 State mode=State::Uninitialized;
 bool available=true, failConfig=false, failTx=false, stuck=false;
 uint32_t epoch=0, finish=0;
 TxStatus tx=TxStatus::None;
 std::vector<uint8_t> sending, pending;
 PacketInfo measurement;
 struct Target { FakeRadioAdapter* radio; uint32_t epoch; };
 std::vector<Target> targets;
 bool begin(const Config& c) override { pending.clear(); tx=TxStatus::None; if(!available) return false; settings=c; mode=State::Idle; ++epoch; return true; }
 bool configure(const Config& c) override { if(!available || failConfig) return false; settings=c; pending.clear(); ++epoch; return true; }
 bool idle() override { mode=State::Idle; pending.clear(); ++epoch; return available; }
 bool receive() override { if(mode!=State::Receiving) ++epoch; mode=State::Receiving; return available; }
 bool transmit(const uint8_t* p,size_t n) override {
  if(!available || failTx) return false;
  pending.clear(); sending.assign(p,p+n); targets.clear();
  mode=State::Transmitting; ++epoch; tx=TxStatus::InProgress;
  finish=channel.now+RadioManager::airtimeMs(settings,n);
  for(auto r:channel.radios) if(r!=this && r->available && r->mode==State::Receiving && compatible(settings,r->settings)) targets.push_back({r,r->epoch});
  return true;
 }
 TxStatus pollTx() override { return available ? tx : TxStatus::Failed; }
 Status read(uint8_t* p,size_t capacity,PacketInfo& info) override {
  if(!available) return Status::HardwareError;
  if(pending.empty()) return Status::NoPacket;
  info=measurement; if(capacity<pending.size()) return Status::BufferTooSmall;
  std::copy(pending.begin(),pending.end(),p); pending.clear(); return Status::Ok;
 }
};
inline void Channel::advance(uint32_t ms) {
 now+=ms;
 for(auto a:radios) if(a->mode==State::Transmitting && !a->stuck && int32_t(now-a->finish)>=0) {
  if(!drop && a->available) for(auto target:a->targets) {
   auto r=target.radio;
   if(r->available && r->mode==State::Receiving && r->epoch==target.epoch && r->pending.empty() && compatible(a->settings,r->settings)) {
    r->pending=a->sending; r->measurement.length=a->sending.size(); r->measurement.rssi=rssi; r->measurement.snr=snr;
   }
  }
  a->mode=State::Idle; a->tx=TxStatus::Complete;
 }
}
}
