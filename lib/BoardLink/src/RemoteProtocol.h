#pragma once
#include "BoardWire.h"
#include <PacketService.h>
#include <StatisticsManager.h>
namespace board_link {
struct PacketActivity {
 protocol::PacketType type=protocol::PacketType::Data;
 bool transmitting=false;
 uint32_t startMs=0, durationMs=0;
 uint8_t flags=0, profile=packet_statistics::UnknownProfile;
};
class ActivityReceiver {
public:
 virtual ~ActivityReceiver() = default;
 // Observer only: do not issue RPCs or block inside this callback.
 virtual void onPacketActivity(const PacketActivity&) = 0;
};
// Confirmed short RPCs preserve PacketService's immediate command semantics.
// RX callbacks are deferred until poll(), never invoked from a waiting RPC.
class RemoteProtocol : public PacketService, public Frames {
public:
 RemoteProtocol(IO& io,StatisticsManager& stats):Frames(io),stats_(stats) {}
 bool connect(uint32_t session);
 radio::Status initialize(const radio::Config&);
 void poll();
 void disconnect() { lost(); }
#ifdef SPLIT_DIAGNOSTICS
 protocol::Result diagnostic(uint8_t mode);
#endif
 void setActivityReceiver(ActivityReceiver* receiver) { activity_=receiver; }
 bool connected() const { return connected_; }
 void setDataReceiver(protocol::DataReceiver* p) override { data_=p; }
 void setLinkReceiver(protocol::LinkReceiver* p) override { link_=p; }
 radio::Status receive() override;
 radio::Status idle() override;
 radio::Status configureRadio(const radio::Config&) override;
 const radio::Config& radioConfig() const override { return config_; }
 radio::State radioState() const override { return state_; }
 radio::TxStatus transmission() const override { return tx_; }
 uint32_t airtimeMs(size_t n) const override { return radio::RadioManager::airtimeMs(config_,n); }
 void allowDataTransmission(bool) override;
 void setDataWindow(uint32_t) override;
 using PacketService::sendData;
 protocol::Result sendData(const protocol::Header&,const uint8_t*,uint32_t) override;
 protocol::Result sendControl(const protocol::ControlPacket&,uint32_t) override;
 protocol::Result sendBeacon(const protocol::BeaconPacket&,uint32_t) override;
 uint32_t timeouts=0, overflows=0, staleFrames=0, eventGaps=0, clockUncertaintyMs=0;
 // Preserve failure context across reconnects; operation values are board_link::Op.
 uint8_t lastTimeoutOp=0;
 uint32_t lastTimeoutAtMs=0, maxCommandRttMs=0;
 uint32_t clockOffsetMs() const { return offset_; }
private:
 ActivityReceiver* activity_=nullptr;
 void notifyActivity(protocol::PacketType,bool,uint32_t,size_t,uint8_t,const radio::PacketInfo* = nullptr);
 StatisticsManager& stats_;protocol::DataReceiver* data_=nullptr;protocol::LinkReceiver* link_=nullptr;
 radio::Config config_;radio::State state_=radio::State::Uninitialized;radio::TxStatus tx_=radio::TxStatus::None;
 uint32_t session_=0, offset_=0, lastSync_=0;uint16_t nextId_=0,waitingId_=0,eventSequence_=0;
 bool connected_=false,waiting_=false,replied_=false,dispatching_=false,aborted_=false;
 uint8_t result_=0;uint32_t remoteStamp_=0;
 struct Event { Kind kind;uint16_t length;uint8_t bytes[Capacity-12]; } events_[16];
 uint8_t eventHead_=0,eventCount_=0;
 uint8_t scratch_[Capacity-12];protocol::Fragment fragment_;
 void frame(Kind,uint16_t,uint32_t,const uint8_t*,size_t) override;
 bool request(Writer&);void lost();void dispatch(const Event&);
 radio::Status simple(Op);
};
}
