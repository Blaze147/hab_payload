#pragma once
#include <PacketService.h>
#include <StatisticsManager.h>
#include "MessageCodec.h"

namespace messaging {
struct MessageView {
 uint16_t transmitterSessionId=0, messageId=0;
 const uint8_t* data=nullptr;
 size_t length=0;
};
class Receiver {
public:
 virtual ~Receiver() = default;
 // Borrowed views; copy before returning if retaining data. Typed callbacks
 // follow onMessage for a validated typed envelope. Legacy bytes stay raw.
 virtual void onMessage(const MessageView&) = 0;
 virtual void onSensors(const SensorData&,const MessageView&) {}
 virtual void onCamera(const CameraView&,const MessageView&) {}
};
}
class MessageManager : public protocol::DataReceiver {
public:
 // Caller-owned large buffers live only on the application host, never on the
 // AVR radio coprocessor. Without Storage, legacy single-packet RX still works.
#ifdef __AVR__
 static constexpr size_t StorageCapacity=512;
#else
 static constexpr size_t StorageCapacity=protocol::MaxMessageSize;
#endif
 struct Storage {
  uint8_t tx[StorageCapacity], rx[StorageCapacity];
 };
 MessageManager(PacketService& protocol,StatisticsManager& statistics)
  : protocol_(protocol),statistics_(statistics) {}
 MessageManager(PacketService& protocol,StatisticsManager& statistics,Storage& storage)
  : protocol_(protocol),statistics_(statistics),storage_(&storage) {}
 void begin(uint16_t session=1);
 void setReceiver(messaging::Receiver* receiver) { receiver_=receiver; }
 void poll(uint32_t nowMs);
 // Ok means copied into the single outbound slot, not delivered remotely.
 // Manager assigns session/message IDs; MessageView identity is RX metadata.
 protocol::Result send(const messaging::MessageView&,uint32_t nowMs);
 protocol::Result sendSensors(const messaging::SensorData&,uint32_t nowMs);
 protocol::Result sendCamera(const messaging::CameraView&,uint32_t nowMs);
 const radio::Config& radioConfig() const { return protocol_.radioConfig(); }
 uint32_t airtimeMs(size_t length) const { return protocol_.airtimeMs(protocol::packetSize(length)); }
 bool ready(uint32_t now) const { return !busy() && !dispatching_ && (lastResult_==protocol::Result::NotReady || int32_t(now-nextTx_)>=0) && protocol_.transmission()!=radio::TxStatus::InProgress; }
 bool busy() const { return txLength_!=0; }
 void setInterPacketPause(uint16_t ms) { pauseMs_=ms; }
 protocol::Result lastSendResult() const { return lastResult_; }
 protocol::Result onFragment(const protocol::Fragment&) override;
 static constexpr uint32_t ReassemblyGapMs=120000, SendLifetimeMs=3600000;
private:
 protocol::Result deliver(const messaging::MessageView&);
 void expire(uint32_t now);
 PacketService& protocol_;
 StatisticsManager& statistics_;
 Storage* storage_=nullptr;
 messaging::Receiver* receiver_=nullptr;
 uint16_t session_=1,nextId_=0,txSession_=0,txId_=0,txIndex_=0,txCount_=0,pauseMs_=0;
 size_t txLength_=0;
 uint32_t txStarted_=0,nextTx_=0;
 protocol::Result lastResult_=protocol::Result::NotReady;
 bool rxActive_=false,dispatching_=false;
 uint16_t rxSession_=0,rxId_=0,rxCount_=0,rxReceived_=0;
 size_t rxLength_=0;
 uint32_t rxAt_=0;
 uint8_t rxSeen_[(protocol::MaxFragmentCount+7)/8]={};
 struct Completed { uint16_t session=0,id=0;uint32_t at=0;bool valid=false; } completed_[8];
 uint8_t completedNext_=0;
};
