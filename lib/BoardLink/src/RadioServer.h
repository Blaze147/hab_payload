#pragma once
#include "BoardWire.h"
#include <ProtocolManager.h>
namespace board_link {
class RadioServer : public Frames, public packet_statistics::Sink, public protocol::DataReceiver, public protocol::LinkReceiver {
public:
 RadioServer(IO& io,radio::RadioManager& radio):Frames(io),radio_(radio),protocol_(radio,*this) {}
 void begin();
 void poll();
 void observe(const packet_statistics::Observation&) override;
 void recordRadioError() override;
 protocol::Result onFragment(const protocol::Fragment&) override;
 protocol::Result onControl(const protocol::ControlPacket&,const radio::PacketInfo&) override;
 protocol::Result onBeacon(const protocol::BeaconPacket&,const radio::PacketInfo&) override;
 uint32_t commands=0, duplicateCommands=0, rejectedCommands=0;
 bool connected() const { return connected_; }
private:
 radio::RadioManager& radio_;ProtocolManager protocol_;
 uint8_t request_[255], scratch_[290];size_t requestLength_=0;
 uint32_t session_=0,requestSession_=0,lastContact_=0;
 uint16_t requestId_=0,lastId_=0,eventSequence_=0;
 uint8_t lastResult_=0;bool pending_=false,connected_=false,haveLast_=false;
 radio::State lastState_=radio::State::Uninitialized;radio::TxStatus lastTx_=radio::TxStatus::None;
 void frame(Kind,uint16_t,uint32_t,const uint8_t*,size_t) override;
 void execute();void status(Kind,uint16_t,uint8_t);
 void event(Kind,Writer&);
};
}
