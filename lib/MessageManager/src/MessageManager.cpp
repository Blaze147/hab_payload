#include "MessageManager.h"
#include <string.h>
void MessageManager::begin(uint16_t session) {
 receiver_=nullptr;session_=session;nextId_=0;txLength_=0;nextTx_=0;
 rxActive_=false;dispatching_=false;completedNext_=0;lastResult_=protocol::Result::NotReady;
 for(auto& c:completed_)c=Completed{};
 protocol_.setDataReceiver(this);
}
void MessageManager::expire(uint32_t now) {
 if(rxActive_ && int32_t(now-rxAt_)>=int32_t(ReassemblyGapMs)) {
  rxActive_=false;statistics_.recordMessageRejected();
 }
}
void MessageManager::poll(uint32_t now) {
 expire(now);
 if(!txLength_ || dispatching_)return;
 if(uint32_t(now-txStarted_)>=SendLifetimeMs) {
  txLength_=0;lastResult_=protocol::Result::NotReady;statistics_.recordMessageRejected();return;
 }
 if(int32_t(now-nextTx_)<0 || protocol_.transmission()==radio::TxStatus::InProgress)return;
 const size_t offset=size_t(txIndex_)*protocol::MaxPayloadSize;
 const size_t remaining=txLength_-offset;
 protocol::Header h;h.transmitterSessionId=txSession_;h.messageId=txId_;
 h.fragmentCount=txCount_;h.fragmentIndex=txIndex_;
 h.payloadLength=uint8_t(remaining>protocol::MaxPayloadSize?protocol::MaxPayloadSize:remaining);
 lastResult_=protocol_.sendData(h,storage_->tx+offset,now);
 if(lastResult_==protocol::Result::Ok) {
  nextTx_=now+protocol_.airtimeMs(protocol::packetSize(h.payloadLength))+pauseMs_;
  if(++txIndex_==txCount_)txLength_=0;
 } else if(lastResult_==protocol::Result::InvalidArgument || lastResult_==protocol::Result::NotImplemented) {
  txLength_=0;statistics_.recordMessageRejected();
 } else nextTx_=now+250; // Window closure, disconnected transport or radio busy.
}
protocol::Result MessageManager::send(const messaging::MessageView& m,uint32_t now) {
 if(!m.data || (!protocol::validMessageSize(m.length) || m.length>StorageCapacity)) {
  statistics_.recordMessageRejected();return protocol::Result::InvalidArgument;
 }
 if(dispatching_ || busy())return protocol::Result::Busy;
 if(!storage_)return protocol::Result::NotReady;
 if(m.data!=storage_->tx)memcpy(storage_->tx,m.data,m.length);
 txLength_=m.length;txStarted_=now;txIndex_=0;
 txCount_=uint16_t((m.length+protocol::MaxPayloadSize-1)/protocol::MaxPayloadSize);
 txSession_=session_;txId_=nextId_++;
 if(!nextId_)++session_;
 // Keep inter-message pacing too, but handle a first send near millis wrap.
 if(int32_t(now-nextTx_)>=0 || lastResult_==protocol::Result::NotReady)nextTx_=now;
 poll(now);return protocol::Result::Ok;
}
protocol::Result MessageManager::sendSensors(const messaging::SensorData& sensors,uint32_t now) {
 uint8_t bytes[messaging::MaxSensorBytes];
 const size_t length=messaging::encodeSensors(sensors,bytes,sizeof(bytes));
 messaging::MessageView m;m.data=bytes;m.length=length;return send(m,now);
}
protocol::Result MessageManager::sendCamera(const messaging::CameraView& camera,uint32_t now) {
 if(dispatching_ || busy())return protocol::Result::Busy;
 if(!storage_)return protocol::Result::NotReady;
 const size_t length=messaging::encodeCamera(camera,now,storage_->tx,sizeof(storage_->tx));
 messaging::MessageView m;m.data=storage_->tx;m.length=length;return send(m,now);
}
protocol::Result MessageManager::deliver(const messaging::MessageView& m) {
 messaging::SensorData sensors;messaging::CameraView camera;
 const bool typed=m.length && m.data[0]==messaging::CodecVersion;
 if(typed && !messaging::decode(m.data,m.length,sensors,camera)) {
  statistics_.recordMessageRejected();return protocol::Result::InvalidArgument;
 }
 dispatching_=true;statistics_.recordMessageCompleted();
 receiver_->onMessage(m);
 if(typed) {
  if(camera.jpeg)receiver_->onCamera(camera,m);
  else receiver_->onSensors(sensors,m);
 }
 dispatching_=false;return protocol::Result::Ok;
}
protocol::Result MessageManager::onFragment(const protocol::Fragment& p) {
 const auto& h=p.header;const uint32_t now=p.reception.receivedAtMs;
 expire(now);
 if(dispatching_)return protocol::Result::Busy;
 if(!protocol::valid(h) || size_t(h.fragmentIndex)*protocol::MaxPayloadSize+h.payloadLength>StorageCapacity ||
    (h.fragmentCount>1 && size_t(h.fragmentCount-1)*protocol::MaxPayloadSize+1>StorageCapacity)) { statistics_.recordMessageRejected();return protocol::Result::InvalidArgument; }
 if(!receiver_)return protocol::Result::NotConnected;
 for(const auto& c:completed_)if(c.valid && c.session==h.transmitterSessionId && c.id==h.messageId &&
  uint32_t(now-c.at)<ReassemblyGapMs)return protocol::Result::Ok;
 messaging::MessageView m;m.transmitterSessionId=h.transmitterSessionId;m.messageId=h.messageId;
 if(rxActive_ && rxSession_==h.transmitterSessionId && rxId_==h.messageId && rxCount_!=h.fragmentCount)
  { statistics_.recordMessageRejected();return protocol::Result::InvalidArgument; }
 if(h.fragmentCount==1) { m.data=p.payload();m.length=h.payloadLength; }
 else {
  if(!storage_)return protocol::Result::NotReady;
  if(rxActive_ && (rxSession_!=h.transmitterSessionId || rxId_!=h.messageId))return protocol::Result::Busy;
  if(!rxActive_) {
   rxActive_=true;rxSession_=h.transmitterSessionId;rxId_=h.messageId;rxCount_=h.fragmentCount;
   rxReceived_=0;rxLength_=0;memset(rxSeen_,0,sizeof(rxSeen_));rxAt_=now;
  }
  const size_t offset=size_t(h.fragmentIndex)*protocol::MaxPayloadSize;
  const uint8_t mask=uint8_t(1u<<(h.fragmentIndex%8));auto& seen=rxSeen_[h.fragmentIndex/8];
  if(seen&mask) {
   if(memcmp(storage_->rx+offset,p.payload(),h.payloadLength) ||
    (h.fragmentIndex+1==h.fragmentCount && rxLength_!=offset+h.payloadLength)) {
    rxActive_=false;statistics_.recordMessageRejected();return protocol::Result::InvalidArgument;
   }
   return protocol::Result::Ok;
  }
  memcpy(storage_->rx+offset,p.payload(),h.payloadLength);seen|=mask;++rxReceived_;rxAt_=now;
  if(h.fragmentIndex+1==h.fragmentCount)rxLength_=offset+h.payloadLength;
  if(rxReceived_!=rxCount_)return protocol::Result::Ok;
  m.data=storage_->rx;m.length=rxLength_;rxActive_=false;
 }
 const auto result=deliver(m);
 if(result==protocol::Result::Ok) {
  auto& c=completed_[completedNext_++%8];c.session=h.transmitterSessionId;c.id=h.messageId;c.at=now;c.valid=true;
 }
 return result;
}
