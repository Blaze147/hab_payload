#pragma once
#include "UsbOutput.h"
#include <Arduino.h>
#include <MessageManager.h>
#include <stdarg.h>
#include <RemoteProtocol.h>
#include <LinkManager.h>
#include "GroundLcd.h"

namespace ground_telemetry {
// USB output is bounded and drained in small chunks; never wait for a monitor
// inside the radio receive callback. No filesystem work runs on this core.
class Receiver : public messaging::Receiver, public board_link::ActivityReceiver {
public:
 uint32_t received=0,dropped=0,activityDropped=0;
 bool idle() const { return count==0; }
 void onPacketActivity(const board_link::PacketActivity& a) override {
  if(!Serial) return;
  const uint32_t sequence=++eventSequence;
  if(count==Capacity) { ++activityDropped;return; }
  auto& line=lines[(head+count)%Capacity];line.length=0;
  const char* type=a.type==protocol::PacketType::Data ? "data" : a.type==protocol::PacketType::Control ? "control" : "beacon";
  append(line,"GROUND_EVENT kind=packet seq=%lu ms=%lu startMs=%lu durationMs=%lu direction=%s type=%s flags=%u profile=P%u\r\n",
   (unsigned long)sequence,(unsigned long)millis(),(unsigned long)a.startMs,(unsigned long)a.durationMs,
   a.transmitting?"tx":"rx",type,unsigned(a.flags),unsigned(a.profile));
  ++count;
 }
 void pollLink(const LinkManager& link,uint32_t now) {
  if(!Serial) { lastPhase=255;return; }
  const auto phase=link.phase();
  const uint32_t start=link.phaseStartedAtMs();
  const bool changed=uint8_t(phase)!=lastPhase || start!=lastStart;
  if(!changed && uint32_t(now-lastWindowReport)<2000) return;
  const uint32_t sequence=++eventSequence;
  if(count==Capacity) { ++activityDropped;return; } // Retry the phase snapshot later.
  if(changed) observedStart=now;
  lastPhase=uint8_t(phase);lastStart=start;lastWindowReport=now;
  const char* name=phase==LinkManager::Phase::Data ? (link.established()?"data":"acquiring") :
   phase==LinkManager::Phase::Control ? "control" : phase==LinkManager::Phase::Beacon ? "beacon" :
   phase==LinkManager::Phase::Searching ? "searching" : phase==LinkManager::Phase::Fault ? "fault" : "stopped";
  const bool scheduled=phase==LinkManager::Phase::Data || phase==LinkManager::Phase::Control || phase==LinkManager::Phase::Beacon;
  auto& line=lines[(head+count)%Capacity];line.length=0;
  append(line,"GROUND_EVENT kind=window seq=%lu ms=%lu startMs=%lu durationMs=%lu phase=%s profile=P%u\r\n",
   (unsigned long)sequence,(unsigned long)now,(unsigned long)(scheduled?start:observedStart),
   (unsigned long)(scheduled?link.phaseEndsAtMs()-start:0),name,unsigned(link.dataProfile()));
  ++count;
 }
 void onMessage(const messaging::MessageView& message) override {
  ++received;
  if(message.length>messaging::MaxSensorBytes) { ++dropped;return; } // Image consumer uses onCamera; never truncate a message into USB hex.
  if(count==Capacity) { ++dropped;return; }
  auto& line=lines[(head+count)%Capacity];line.length=0;
  append(line,"GROUND_RX ms=%lu session=%u message=%u bytes=%u hex=",
   (unsigned long)millis(),message.transmitterSessionId,message.messageId,unsigned(message.length));
  for(size_t i=0;i<message.length;++i) append(line,"%02X",unsigned(message.data[i]));
  append(line,"\r\n");++count;
 }
 void onSensors(const messaging::SensorData& s,const messaging::MessageView&) override {
  if(s.gps && (s.gpsFlags&2))ground_lcd::updatePosition(s.latitudeE6,s.longitudeE6,millis());
  // Detailed numeric decoding belongs to MessageManager. GROUND_RX above
  // exposes the validated message for the host bridge, independent of LCD/USB.
 }
 void poll() {
  if(!Serial) { head=count=0;offset=0;lastPhase=255;return; }
  if(!count) return;
  auto& line=lines[head];
  const int available=usb_output::availableForWrite();
  if(available<=0) return;
  const size_t remaining=line.length-offset;
  const size_t amount=remaining<size_t(available)?remaining:size_t(available);
  offset+=Serial.write(reinterpret_cast<const uint8_t*>(line.bytes+offset),amount);
  if(offset==line.length) { offset=0;head=(head+1)%Capacity;--count; }
 }
private:
 static constexpr uint8_t Capacity=8;
 uint32_t eventSequence=0,lastStart=0,lastWindowReport=0,observedStart=0;uint8_t lastPhase=255;
 struct Line { char bytes[900];size_t length=0; } lines[Capacity];
 uint8_t head=0,count=0;size_t offset=0;
 static void append(Line& line,const char* format,...) {
  const size_t room=sizeof(line.bytes)-line.length;
  va_list args;va_start(args,format);
  const int result=vsnprintf(line.bytes+line.length,room,format,args);
  va_end(args);
  if(result>0) line.length+=size_t(result)<room?size_t(result):room-1;
 }
};
static Receiver receiver;
}
