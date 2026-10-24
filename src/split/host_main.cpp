#include <Arduino.h>
#include <RemoteProtocol.h>
#include <LinkManager.h>
#include <MessageManager.h>
#include "BoardSerial.h"
#include "UsbOutput.h"
#ifdef RADIO_TRANSMITTER
#include "apps/BalloonApplication.h"
#elif defined(RADIO_RECEIVER)
#include "apps/GroundApplication.h"
#else
#error Select a balloon or ground host role
#endif
#include "StatusLed.h"
#include <pico/rand.h>
#ifdef APPLICATION_WATCHDOG
#include "ApplicationWatchdog.h"
#endif
namespace {
#ifdef SPLIT_DIAGNOSTICS
class TestReceiver : public messaging::Receiver {
public:
 uint32_t count=0, invalid=0;
 void onMessage(const messaging::MessageView& m) override {
  ++count;if(m.length!=240 || m.transmitterSessionId!=0x1234) { ++invalid;return; }
  for(size_t i=0;i<m.length;++i) if(m.data[i]!=uint8_t(i)) { ++invalid;break; }
 }
} testReceiver;
#endif
BoardSerial uart;
StatisticsManager statistics;
StatisticsManager::DenseStorage dense;
board_link::RemoteProtocol packets(uart,statistics);
class PacketActivityReceiver : public board_link::ActivityReceiver {
 void onPacketActivity(const board_link::PacketActivity& packet) override {
  status_led::onPacketActivity(packet);
  endpoint_application::onPacketActivity(packet);
 }
} packetActivityReceiver;
MessageManager::Storage messageStorage;
MessageManager messages(packets,statistics,messageStorage);
LinkManager link(packets,statistics);
uint32_t retryAt=0,reportAt=0;bool started=false, enabled=true;
uint32_t linkStarts=0,maxLoopGapMs=0,lastLoopAt=0;
bool due(uint32_t now,uint32_t at) { return int32_t(now-at)>=0; }
void startLink() {
 LinkManager::Config c;
 endpoint_application::configureLink(c);
 const auto result=link.start(endpoint_application::Role,c,millis());started=result==protocol::Result::Ok;
 if(started) ++linkStarts;
 if(Serial && usb_output::availableForWrite()>=64 && endpoint_application::outputReady()) { Serial.print("LINK_START result=");Serial.println(uint8_t(result)); }
}
#ifdef SPLIT_DIAGNOSTICS
void selfTest() {
 enabled=false;link.stop();started=false;
 const uint32_t settleAt=millis();
 while(packets.transmission()==radio::TxStatus::InProgress && uint32_t(millis()-settleAt)<8000) { packets.poll();yield(); }
 const auto initialTimeouts=packets.timeouts, initialBad=packets.badFrames, initialOverflow=packets.overflows;
 unsigned passed=0,failed=0;
 auto check=[&](bool ok) {
  if(ok) ++passed;
  else { ++failed;Serial.print("SELFTEST_FAIL check=");Serial.println(passed+failed); }
 };
 check(packets.connected());check(packets.idle()==radio::Status::Ok);
 auto c=radio::Config{};c.spreadingFactor=7;c.bandwidth=500000;c.codingRate=5;
 check(packets.configureRadio(c)==radio::Status::Ok);check(packets.radioConfig().spreadingFactor==7);
 messages.setReceiver(&testReceiver);
 const auto before=statistics.packetDiagnostics(4);const auto count=testReceiver.count;
 for(unsigned i=0;i<32;++i) {
  check(packets.diagnostic(0)==protocol::Result::Ok);packets.poll();
 }
 check(testReceiver.count-count==32);check(testReceiver.invalid==0);
 check(packets.diagnostic(1)==protocol::Result::InvalidArgument);packets.poll();
 check(packets.diagnostic(2)==protocol::Result::InvalidArgument);packets.poll();
 const auto after=statistics.packetDiagnostics(4);
 check(after.packets-before.packets==34);check(after.corruptPackets-before.corruptPackets==1);
 check(after.malformedPackets-before.malformedPackets==1);check(after.wireBytes-before.wireBytes==34*255);
 packets.setDataWindow(millis()+1);
 protocol::Header h;h.payloadLength=1;const uint8_t payload=42;
 check(packets.sendData(h,&payload,millis())==protocol::Result::Busy);
 packets.allowDataTransmission(true);
 // Physical TX tests confirm local completion; no remote RF receiver is attached.
 for(unsigned i=0;i<10;++i) {
  check(packets.sendData(h,&payload,millis())==protocol::Result::Ok);
  const uint32_t start=millis();
  while(packets.transmission()==radio::TxStatus::InProgress && uint32_t(millis()-start)<2000) { packets.poll();yield(); }
  check(packets.transmission()==radio::TxStatus::Complete);
 }
 for(unsigned i=0;i<100;++i) check(packets.receive()==radio::Status::Ok);
 check(packets.configureRadio(radio::Config{})==radio::Status::Ok);check(packets.idle()==radio::Status::Ok);
 check(packets.timeouts==initialTimeouts);check(packets.badFrames==initialBad);check(packets.overflows==initialOverflow);
 messages.setReceiver(nullptr);
 Serial.print("SELFTEST passed=");Serial.print(passed);Serial.print(" failed=");Serial.print(failed);
 Serial.print(" messages=");Serial.print(testReceiver.count-count);Serial.print(" invalid=");Serial.println(testReceiver.invalid);
}
#endif

// A complete report exceeds CDC's FIFO. Build it locally and drain only the
// space currently available, so loss of USB cannot stall radio/link service.
class ReportOutput : public Print {
 uint8_t bytes_[2048];size_t length_=0,offset_=0;
public:
 using Print::write;
 bool pending() const {return offset_<length_;}
 void begin() {length_=offset_=0;usb_output::reportPending=false;}
 size_t write(uint8_t value) override {
  if(length_==sizeof(bytes_))return 0;
  bytes_[length_++]=value;usb_output::reportPending=true;return 1;
 }
 void poll() {
  if(!Serial) {begin();return;}
  const int room=Serial.availableForWrite();
  if(room<=0 || !pending())return;
  size_t count=length_-offset_;
  if(count>size_t(room))count=size_t(room);
  if(count>128)count=128;
  offset_+=Serial.write(bytes_+offset_,count);
  if(!pending())usb_output::reportPending=false;
 }
} reportOutput;
void report() {
 if(reportOutput.pending())return;
 reportOutput.begin();
 if(!endpoint_application::outputReady()) return;
 const auto s=statistics.snapshot();
 reportOutput.print("SPLIT_HOST connected=");reportOutput.print(packets.connected());reportOutput.print(" state=");reportOutput.print(uint8_t(packets.radioState()));
 reportOutput.print(" restartFlagSupported=");reportOutput.print(packets.restartFlagSupported());
 reportOutput.print(" phase=");reportOutput.print(uint8_t(link.phase()));reportOutput.print(" established=");reportOutput.print(link.established());
 reportOutput.print(" conversation=");reportOutput.print(uint8_t(link.conversation()));
 reportOutput.print(" probing=");reportOutput.print(link.probing());
 reportOutput.print(" probeReason=");reportOutput.print(uint8_t(link.probeReason()));
 reportOutput.print(" peakAirtimeGoodputBps=");reportOutput.print(link.peakAirtimeGoodput(link.dataProfile()));
 reportOutput.print(" probeNegotiating=");reportOutput.print(link.negotiatingProbe());
 reportOutput.print(" degradedDataWindows=");reportOutput.print(link.degradedDataWindows());
 reportOutput.print(" silentDataWindows=");reportOutput.print(link.silentDataWindows());
 reportOutput.print(" failedProbeWindows=");reportOutput.print(link.failedProbeWindows());
 reportOutput.print(" dataProfile=P");reportOutput.print(uint8_t(link.dataProfile()));
 if(const auto* record=statistics.profileRecord(uint8_t(link.dataProfile()))) {
  const auto recent=record->recent(millis()), whole=record->whole(millis());
  reportOutput.print(" profileInstance=");reportOutput.print(record->instanceId);
  reportOutput.print(" wallGoodputBps=");reportOutput.print(recent.wallGoodputBytesPerSecond());
  reportOutput.print(" airtimeGoodputBps=");reportOutput.print(recent.airtimeGoodputBytesPerSecond());
  reportOutput.print(" recentPackets=");reportOutput.print(recent.packets.packets);
  reportOutput.print(" instanceWallGoodputBps=");reportOutput.print(whole.wallGoodputBytesPerSecond());
  reportOutput.print(" instanceAirtimeGoodputBps=");reportOutput.print(whole.airtimeGoodputBytesPerSecond());
  reportOutput.print(" recentMeanRssi=");reportOutput.print(recent.packets.meanRssi);
  reportOutput.print(" recentMeanSnr=");reportOutput.print(recent.packets.meanSnr);
  reportOutput.print(" instanceMeanRssi=");reportOutput.print(whole.packets.meanRssi);
  reportOutput.print(" instanceMeanSnr=");reportOutput.print(whole.packets.meanSnr);
 }
 reportOutput.print(" missed=");reportOutput.print(link.missedControlWindows());
 reportOutput.print(" linkResult=");reportOutput.print(uint8_t(link.lastResult()));
 reportOutput.print(" rx=");reportOutput.print(s.receivedPackets);reportOutput.print(" radioErrors=");reportOutput.print(s.radioErrors);
 reportOutput.print(" timeouts=");reportOutput.print(packets.timeouts);reportOutput.print(" bad=");reportOutput.print(packets.badFrames);
 reportOutput.print(" eventGaps=");reportOutput.print(packets.eventGaps);
 reportOutput.print(" lastTimeoutOp=");reportOutput.print(packets.lastTimeoutOp);
 reportOutput.print(" lastTimeoutAtMs=");reportOutput.print(packets.lastTimeoutAtMs);
 reportOutput.print(" maxCommandRttMs=");reportOutput.print(packets.maxCommandRttMs);
 reportOutput.print(" linkStarts=");reportOutput.print(linkStarts);
 reportOutput.print(" maxLoopGapMs=");reportOutput.print(maxLoopGapMs);
 reportOutput.print(" uptimeMs=");reportOutput.print(millis());
 reportOutput.print(" phaseEndMs=");reportOutput.print(link.phaseEndsAtMs());
 reportOutput.print(" overflow=");reportOutput.print(packets.overflows);reportOutput.print(" clockUncertaintyMs=");reportOutput.print(packets.clockUncertaintyMs);
 reportOutput.print(" messages=");reportOutput.print(s.completedMessages);
 if(s.hasReceiveMeasurement) {
  reportOutput.print(" rssi=");reportOutput.print(s.lastRssi);reportOutput.print(" snr=");reportOutput.print(s.lastSnr);
 }
 const auto p0=statistics.profile(0);
 reportOutput.print(" p0Packets=");reportOutput.print(p0.packets);reportOutput.print(" p0Data=");reportOutput.print(p0.dataPackets);
 reportOutput.print(" p0Corrupt=");reportOutput.print(p0.corruptPackets);reportOutput.print(" p0Malformed=");reportOutput.println(p0.malformedPackets);
}
}
#ifdef APPLICATION_WATCHDOG
// Arduino calls this before USB initialization and before launching core 1.
void initVariant() { application_watchdog::begin(); }
#endif
void setup() {
 status_led::begin();
 Serial.begin(115200);uart.begin();statistics.begin();statistics.setDenseLogging(dense);messages.begin(uint16_t(get_rand_32()));link.begin();
 endpoint_application::begin(messages);packets.setActivityReceiver(&packetActivityReceiver);
}
void loop() {
#ifdef APPLICATION_WATCHDOG
 application_watchdog::stage(10);
#endif
 const uint32_t loopAt=millis();
 if(lastLoopAt && uint32_t(loopAt-lastLoopAt)>maxLoopGapMs) maxLoopGapMs=loopAt-lastLoopAt;
 lastLoopAt=loopAt;
 status_led::poll(millis());
 endpoint_application::pollSensors(millis());
#ifdef APPLICATION_WATCHDOG
 application_watchdog::stage(30);
#endif
 packets.poll();
 const uint32_t now=millis();
 if(!packets.connected()) {
#ifdef APPLICATION_WATCHDOG
 application_watchdog::stage(40);
#endif
  started=false;
  if(due(now,retryAt)) {
   retryAt=now+2000;
   if(packets.connect(get_rand_32())) {
    if(packets.initialize(radio::Config{})==radio::Status::Ok) {
     if(Serial && usb_output::availableForWrite()>=64 && endpoint_application::outputReady()) Serial.println("RADIO_READY uart=100000 tx=GP0 rx=GP1");
    } else packets.disconnect();
   }
  }
 }
 if(packets.connected() && enabled && !started) startLink();
#ifdef APPLICATION_WATCHDOG
 application_watchdog::stage(50);
#endif
 messages.poll(millis());if(started) link.poll(millis());
 endpoint_application::pollLink(link,millis());
 reportOutput.poll();
 if(started && enabled && link.phase()==LinkManager::Phase::Data)
  endpoint_application::pollData(messages, millis());
#ifdef APPLICATION_WATCHDOG
 application_watchdog::stage(70);
#endif
 if(Serial && Serial.available()) {
  const char c=char(Serial.read());
  if(c=='s') report();
#ifdef SPLIT_DIAGNOSTICS
  if(c=='t') selfTest();
#endif
  if(c=='x') { enabled=false;link.stop();packets.idle();started=false;if(endpoint_application::outputReady()) Serial.println("LINK_STOPPED"); }
  if(c=='r') { enabled=true;link.stop();started=false; }
 }
 if(Serial && uint32_t(now-reportAt)>=2000 && endpoint_application::outputReady()) {
#ifdef APPLICATION_WATCHDOG
  application_watchdog::stage(80);
#endif
  reportAt=now;report();
 }
#ifdef APPLICATION_WATCHDOG
 application_watchdog::progress();
#endif
}

bool core1_separate_stack = true;
void setup1() {
 endpoint_application::beginCore1();
}
void loop1() {
 endpoint_application::pollCore1(); delay(1);
}
