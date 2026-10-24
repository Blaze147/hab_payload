#include <Arduino.h>
#include <LinkManager.h>
#include "LinkTestFixture.h"
#include "PhysicalRadioAdapter.h"
#ifndef LINK_TEST_PACKET_LIMIT
#define LINK_TEST_PACKET_LIMIT 30UL
#endif
namespace link_test {
namespace {
PhysicalRadioAdapter hardware;
radio::RadioManager radio(hardware);
StatisticsManager statistics;
ProtocolManager protocolManager(radio,statistics);
MessageManager messages(protocolManager,statistics);
LinkManager link(protocolManager,statistics);
#ifdef RADIO_TRANSMITTER
CounterSource source(protocolManager,LINK_TEST_PACKET_LIMIT);
bool dataTxPending=false, limitReported=false;
#else
CounterReceiver sink;
uint32_t reportedReceived=0, reportedInvalid=0;
#endif
radio::Status initialized=radio::Status::NotInitialized;
protocol::Result started=protocol::Result::NotReady;
bool announced=false;
radio::Status lastPollStatus=radio::Status::Ok;
uint8_t lastPhase=255, lastConversation=255, lastProfile=255;
uint32_t nextSummary=0;
const __FlashStringHelper* phaseName(LinkManager::Phase phase) {
 switch(phase) {
 case LinkManager::Phase::Stopped: return F("stopped");
 case LinkManager::Phase::Searching: return F("searching");
 case LinkManager::Phase::Data: return F("data");
 case LinkManager::Phase::Beacon: return F("beacon");
 case LinkManager::Phase::Control: return F("control");
 case LinkManager::Phase::Fault: return F("fault");
 } return F("unknown");
}
const __FlashStringHelper* conversationName(LinkManager::Conversation state) {
 switch(state) {
 case LinkManager::Conversation::Idle: return F("idle");
 case LinkManager::Conversation::SendSyn: return F("send SYN");
 case LinkManager::Conversation::WaitSynAck: return F("wait SYN-ACK");
 case LinkManager::Conversation::SendSynAck: return F("send SYN-ACK");
 case LinkManager::Conversation::WaitAck: return F("wait ACK");
 case LinkManager::Conversation::SendAck: return F("send ACK");
 case LinkManager::Conversation::Complete: return F("complete");
 case LinkManager::Conversation::Provisional: return F("provisional (ACK sent)");
 case LinkManager::Conversation::Exhausted: return F("no remaining frame budget");
 } return F("unknown");
}
void prefix(uint32_t now) { Serial.print(now); Serial.print(F(" ms ")); }
void report(uint32_t now) {
 if(!Serial) { announced=false; return; }
 if(!announced) {
  announced=true; lastPhase=lastConversation=lastProfile=255;
#ifdef RADIO_TRANSMITTER
  Serial.println(F("Integrated P0 test: BALLOON"));
#else
  Serial.println(F("Integrated P0 test: GROUND"));
#endif
  Serial.print(F("Radio init: ")); Serial.println(radio::statusName(initialized));
  Serial.print(F("Link start result (0=OK): ")); Serial.println(uint8_t(started));
  Serial.println(F("P0 fixed; no automatic probing; payload HAB1 + uint32 counter"));
 }
 if(lastPhase!=uint8_t(link.phase()) || lastConversation!=uint8_t(link.conversation()) || lastProfile!=uint8_t(link.dataProfile())) {
  lastPhase=uint8_t(link.phase()); lastConversation=uint8_t(link.conversation()); lastProfile=uint8_t(link.dataProfile());
  prefix(now); Serial.print(F("phase=")); Serial.print(phaseName(link.phase()));
  Serial.print(F(" conversation=")); Serial.print(conversationName(link.conversation()));
  Serial.print(F(" id=")); Serial.print(link.conversationId());
  Serial.print(F(" data=P")); Serial.print(uint8_t(link.dataProfile()));
  Serial.print(F(" missed=")); Serial.println(link.missedControlWindows());
 }
 if(int32_t(now-nextSummary)>=0) {
  nextSummary=now+10000;
  const auto p=statistics.profile(0);
  prefix(now); Serial.print(F("P0 RX=")); Serial.print(p.packets);
  Serial.print(F(" data=")); Serial.print(p.dataPackets);
  Serial.print(F(" CRCbad=")); Serial.print(p.corruptPackets);
  Serial.print(F(" usefulBytes=")); Serial.print(p.uniquePayloadBytes);
  Serial.print(F(" pendingGaps=")); Serial.print(p.pendingMissing);
  if(p.observed) {
   Serial.print(F(" RSSI=")); Serial.print(p.lastRssi);
   Serial.print(F(" SNRx10=")); Serial.println(int16_t(p.lastSnr*10));
  } else Serial.println(F(" signal=unobserved"));
 }
}
}
void setup() {
 Serial.begin(115200); // Never wait for USB: both devices must run without a monitor.
 statistics.begin(); protocolManager.begin(); messages.begin(); link.begin();
#ifndef RADIO_TRANSMITTER
 messages.setReceiver(&sink);
#endif
 initialized=radio.initialize(radio::Config{});
 if(initialized!=radio::Status::Ok) return;
 LinkManager::Config config; config.autoProbe=false;
#ifdef RADIO_TRANSMITTER
 started=link.start(LinkManager::Role::Balloon,config,millis());
#else
 started=link.start(LinkManager::Role::Ground,config,millis());
#endif
}
void loop() {
 const uint32_t now=millis();
 const auto status=protocolManager.poll(now);
#ifdef RADIO_TRANSMITTER
 if(dataTxPending && radio.transmission()==radio::TxStatus::Complete) {
  dataTxPending=false;
  if(Serial) { prefix(now); Serial.println(F("data TX complete (delivery unknown)")); }
 }
#endif
 messages.poll(now); link.poll(now);
 if(Serial && status!=lastPollStatus && status!=radio::Status::Ok && status!=radio::Status::Busy) {
  prefix(now); Serial.print(F("Radio poll: ")); Serial.println(radio::statusName(status));
 }
lastPollStatus=status;
#ifdef RADIO_TRANSMITTER
 if(started==protocol::Result::Ok && link.phase()==LinkManager::Phase::Data && radio.state()==radio::State::Idle) {
  const auto result=source.trySend(now);
  if(result==protocol::Result::Ok) {
   dataTxPending=true;
   if(Serial) { prefix(now); Serial.print(F("data TX accepted counter=")); Serial.println(source.sent()-1); }
  } else if(Serial && result!=protocol::Result::Busy && result!=protocol::Result::NotReady) {
   prefix(now); Serial.print(F("Data send error=")); Serial.println(uint8_t(result));
  }
 }
 if(source.finished() && !dataTxPending && !limitReported && Serial) {
  limitReported=true; Serial.println(F("Counter limit reached; link/control traffic continues."));
 }
#else
 if(Serial && reportedReceived!=sink.received()) {
  reportedReceived=sink.received(); prefix(now); Serial.print(F("data RX counter=")); Serial.print(sink.last());
  Serial.print(F(" count=")); Serial.print(sink.received());
  Serial.print(F(" sequenceErrors=")); Serial.println(sink.sequenceErrors());
 }
 if(Serial && reportedInvalid!=sink.invalid()) {
  reportedInvalid=sink.invalid(); prefix(now); Serial.print(F("invalid test payloads=")); Serial.println(sink.invalid());
 }
#endif
 report(now);
}
}
