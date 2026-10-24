#pragma once
#include <ProtocolManager.h>
#include <MessageManager.h>

// Shared by the opt-in board test and native integration test. Not production data.
namespace link_test {
constexpr uint16_t Session = 0xB001; // Fixed test ID, not a boot-session generator.
constexpr uint8_t PayloadSize = 8;
class CounterSource {
public:
 explicit CounterSource(ProtocolManager& protocol, uint32_t limit=30)
  : protocol_(protocol), limit_(limit) {}
 protocol::Result trySend(uint32_t now) {
  if((limit_ && sent_>=limit_) || int32_t(now-nextAt_)<0) return protocol::Result::NotReady;
  protocol::Header header;
  header.transmitterSessionId=Session; header.messageId=uint16_t(sent_); header.payloadLength=PayloadSize;
  uint8_t payload[PayloadSize]={'H','A','B','1',0,0,0,0};
  for(uint8_t i=0;i<4;++i) payload[4+i]=uint8_t(sent_>>(24-8*i));
  const auto result=protocol_.sendData(header,payload,now);
  if(result==protocol::Result::Ok) {
   ++sent_;
   nextAt_=now+protocol_.airtimeMs(protocol::packetSize(PayloadSize))+1000;
  } else nextAt_=now+100; // Backpressure at a window boundary does not advance the counter.
  return result;
 }
 uint32_t sent() const { return sent_; } // Radio request accepted, not confirmed delivery.
 bool finished() const { return limit_ && sent_>=limit_; }
private:
 ProtocolManager& protocol_;
 uint32_t limit_, sent_=0, nextAt_=0;
};
class CounterReceiver : public messaging::Receiver {
public:
 void onMessage(const messaging::MessageView& message) override {
  if(message.length!=PayloadSize || !message.data || message.transmitterSessionId!=Session ||
     message.data[0]!='H' || message.data[1]!='A' || message.data[2]!='B' || message.data[3]!='1') {
   ++invalid_; return;
  }
  uint32_t value=0;
  for(uint8_t i=0;i<4;++i) value=(value<<8)|message.data[4+i];
  if(message.messageId!=uint16_t(value)) { ++invalid_; return; }
  if(value!=expected_) ++sequenceErrors_;
  last_=value; expected_=value+1; ++received_;
 }
 uint32_t received() const { return received_; }
 uint32_t last() const { return last_; }
 uint32_t invalid() const { return invalid_; }
 uint32_t sequenceErrors() const { return sequenceErrors_; }
private:
 uint32_t received_=0, last_=0, expected_=0, invalid_=0, sequenceErrors_=0;
};
}
