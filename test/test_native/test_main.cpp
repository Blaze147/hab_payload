#include "Simulation.h"
#include "ProtocolManager.h"
#include "LinkProfiles.h"
#include "LinkManager.h"
#include "MessageManager.h"
#include "LinkTestFixture.h"
#include "TelemetrySource.h"
#include <type_traits>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
using namespace radio;
using namespace simulation;
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
struct Pair {
 Channel channel; FakeRadioAdapter a{channel},b{channel}; RadioManager balloon{a},ground{b};
 Config c; uint8_t payload[255],out[255]; PacketInfo info;
 Pair() { for(size_t i=0;i<255;++i) payload[i]=uint8_t(i); CHECK(balloon.initialize(c)==Status::Ok); CHECK(ground.initialize(c)==Status::Ok); CHECK(ground.receive()==Status::Ok); }
 void finish(RadioManager& m) { channel.advance(RadioManager::airtimeMs(m.config(),255)+1); CHECK(m.poll(channel.now)==Status::Ok); CHECK(m.transmission()==TxStatus::Complete); CHECK(m.state()==State::Idle); }
 void send(size_t n=255) { CHECK(balloon.transmit(payload,n,channel.now)==Status::Ok); finish(balloon); }
};
void initialization() {
 Pair p; Config c=p.c;
 CHECK(p.a.settings.frequency==c.frequency); CHECK(p.a.settings.power==c.power);
 for(int sf=0;sf<16;++sf) { c=p.c; c.spreadingFactor=sf; CHECK(RadioManager::valid(c)==(sf>=7&&sf<=12)); }
 for(int cr=0;cr<11;++cr) { c=p.c; c.codingRate=cr; CHECK(RadioManager::valid(c)==(cr>=5&&cr<=8)); }
 for(auto bw:{7800UL,10400UL,15600UL,20800UL,31250UL,41700UL,62500UL,125000UL,250000UL,500000UL}) { c=p.c; c.bandwidth=bw; CHECK(p.balloon.configure(c)==Status::Ok); CHECK(p.a.settings.bandwidth==bw); }
 for(auto f:{0UL,901999999UL,928000001UL}) { c=p.c; c.frequency=f; CHECK(p.balloon.configure(c)==Status::InvalidConfig); }
 c=p.c; c.bandwidth=123; CHECK(!RadioManager::valid(c));
 c=p.c; c.power=21; CHECK(!RadioManager::valid(c)); c.power=1; CHECK(!RadioManager::valid(c));
 c=p.c; c.preamble=5; CHECK(!RadioManager::valid(c));
 FakeRadioAdapter a(p.channel); RadioManager m(a); CHECK(m.receive()==Status::NotInitialized); a.available=false; CHECK(m.initialize(p.c)==Status::HardwareError); a.available=true; CHECK(m.initialize(p.c)==Status::Ok);
}
void packets() {
 Pair p; CHECK(p.balloon.transmit(p.payload,0,0)==Status::InvalidPayload); CHECK(p.balloon.transmit(nullptr,1,0)==Status::InvalidPayload); CHECK(p.balloon.transmit(p.payload,256,0)==Status::InvalidPayload);
 for(auto n:{1,8,255}) {
  CHECK(p.balloon.transmit(p.payload,n,p.channel.now)==Status::Ok);
  CHECK(p.balloon.transmission()==TxStatus::InProgress); CHECK(p.balloon.receive()==Status::Busy); CHECK(p.balloon.configure(p.c)==Status::Busy); CHECK(p.balloon.initialize(p.c)==Status::Busy);
  p.channel.advance(1); CHECK(p.balloon.poll(p.channel.now)==Status::Ok); CHECK(p.balloon.transmission()==TxStatus::InProgress);
  p.finish(p.balloon);
  CHECK(p.ground.read(p.out,0,p.info)==Status::BufferTooSmall); CHECK(p.info.length==size_t(n));
  CHECK(p.ground.read(p.out,255,p.info)==Status::Ok); CHECK(p.info.rssi==-90); CHECK(p.info.snr==7.5f); CHECK(p.info.length==size_t(n));
  for(int i=0;i<n;++i) CHECK(p.out[i]==p.payload[i]);
  CHECK(p.ground.read(p.out,255,p.info)==Status::NoPacket);
 }
 p.channel.drop=true; p.send(); CHECK(p.ground.read(p.out,255,p.info)==Status::NoPacket);
}
void profiles() {
 const link_profiles::Definition definitions[]={link_profiles::P0, link_profiles::P1,
  link_profiles::P2, link_profiles::P3, link_profiles::P4};
 CHECK(link_profiles::Default==link_profiles::Id::P0);
 CHECK(link_profiles::Fallback==link_profiles::Id::P0);
 const uint8_t sf[]={12,10,9,8,7}, cr[]={8,6,5,5,5};
 const uint32_t bw[]={125000,125000,125000,250000,500000};
 for(unsigned id=0;id<5;++id) {
  Pair p; Config c=p.c;
  c.frequency=916000000; c.power=10; c.preamble=12; c.syncWord=0x34; c.crc=false;
  const auto& profile=definitions[id];
  CHECK(static_cast<unsigned>(profile.id)==id);
  c.spreadingFactor=profile.spreadingFactor;
  c.bandwidth=profile.bandwidth; c.codingRate=profile.codingRate;
  CHECK(c.spreadingFactor==sf[id]); CHECK(c.codingRate==cr[id]); CHECK(c.bandwidth==bw[id]);
  CHECK(c.frequency==916000000); CHECK(c.power==10); CHECK(c.preamble==12);
  CHECK(c.syncWord==0x34); CHECK(!c.crc);
  CHECK(p.balloon.configure(c)==Status::Ok); CHECK(p.ground.configure(c)==Status::Ok);
  p.send(); CHECK(p.ground.read(p.out,255,p.info)==Status::Ok);

 }

 for(int field=0;field<7;++field) {
  Pair p; Config c=p.c;
  switch(field) { case 0:c.frequency=916000000;break; case 1:c.bandwidth=250000;break;case 2:c.spreadingFactor=9;break;case 3:c.codingRate=5;break;case 4:c.crc=false;break;case 5:c.syncWord=0x34;break;case 6:c.preamble=12;break; }
  CHECK(p.balloon.configure(c)==Status::Ok); p.send(); CHECK(p.ground.read(p.out,255,p.info)==Status::NoPacket);
  CHECK(p.ground.configure(c)==Status::Ok); CHECK(p.ground.state()==State::Receiving); p.send(); CHECK(p.ground.read(p.out,255,p.info)==Status::Ok);
 }
}
void errorsAndTiming() {
 Pair p; p.a.failConfig=true; CHECK(p.balloon.configure(p.c)==Status::HardwareError); CHECK(p.balloon.state()==State::Fault); p.a.failConfig=false; CHECK(p.balloon.initialize(p.c)==Status::Ok);
 p.a.failTx=true; CHECK(p.balloon.transmit(p.payload,1,0)==Status::HardwareError); p.a.failTx=false; CHECK(p.balloon.initialize(p.c)==Status::Ok);
 p.a.stuck=true; CHECK(p.balloon.transmit(p.payload,1,0)==Status::Ok); CHECK(p.balloon.poll(100000)==Status::Timeout); CHECK(p.balloon.state()==State::Fault); p.a.stuck=false; CHECK(p.balloon.initialize(p.c)==Status::Ok);
 p.a.available=false; CHECK(p.balloon.transmit(p.payload,1,0)==Status::HardwareError); p.a.available=true; CHECK(p.balloon.initialize(p.c)==Status::Ok);
 CHECK(p.balloon.transmit(p.payload,8,p.channel.now)==Status::Ok); CHECK(p.ground.idle()==Status::Ok); CHECK(p.ground.receive()==Status::Ok); p.finish(p.balloon); CHECK(p.ground.read(p.out,255,p.info)==Status::NoPacket);
 CHECK(p.balloon.receive()==Status::Ok); CHECK(p.balloon.transmit(p.payload,8,p.channel.now)==Status::Ok); CHECK(p.ground.transmit(p.payload,8,p.channel.now)==Status::Ok); p.finish(p.balloon); CHECK(p.ground.poll(p.channel.now)==Status::Ok); CHECK(p.balloon.receive()==Status::Ok); CHECK(p.balloon.read(p.out,255,p.info)==Status::NoPacket);
 p.channel.now=UINT_MAX-10; CHECK(p.balloon.transmit(p.payload,8,p.channel.now)==Status::Ok); p.finish(p.balloon);
 CHECK(RadioManager::airtimeMs(p.c,8)==1188);
 p.b.available=false; CHECK(p.ground.receive()==Status::HardwareError); p.b.available=true; CHECK(p.ground.initialize(p.c)==Status::Ok);
}
void bidirectional() {
 Pair p; p.send(8); CHECK(p.ground.read(p.out,255,p.info)==Status::Ok);
 CHECK(p.balloon.receive()==Status::Ok);
 CHECK(p.ground.transmit(p.payload,8,p.channel.now)==Status::Ok); p.finish(p.ground);
 CHECK(p.balloon.read(p.out,255,p.info)==Status::Ok);
 for(size_t i=0;i<8;++i) CHECK(p.out[i]==p.payload[i]);
 CHECK(p.ground.receive()==Status::Ok);
 p.payload[0]=10; p.send(8); p.payload[0]=20; p.send(8);
 CHECK(p.ground.read(p.out,255,p.info)==Status::Ok); CHECK(p.out[0]==10);
 CHECK(p.ground.read(p.out,255,p.info)==Status::NoPacket);
}
void repeated() {
 Pair p;
 for(unsigned i=0;i<1000;++i) {
  RadioManager& tx=(i%2)?p.ground:p.balloon; RadioManager& rx=(i%2)?p.balloon:p.ground;
  CHECK(rx.receive()==Status::Ok); p.payload[0]=uint8_t(i); p.payload[1]=uint8_t(i>>8);
  CHECK(tx.transmit(p.payload,255,p.channel.now)==Status::Ok); p.finish(tx);
  CHECK(rx.read(p.out,255,p.info)==Status::Ok); CHECK(p.info.length==255);
  for(size_t j=0;j<255;++j) CHECK(p.out[j]==p.payload[j]);
 }
}
void protocolStructure() {
 using namespace protocol;
 static_assert(sizeof(decltype(Header::version))==1, "version width");
 static_assert(sizeof(PacketType)==1, "type width");
 static_assert(sizeof(decltype(Header::messageId))==2, "message ID width");
 static_assert(sizeof(decltype(Header::fragmentIndex))==2, "index width");
 static_assert(sizeof(decltype(Header::fragmentCount))==2, "count width");
 static_assert(sizeof(decltype(Header::payloadLength))==1, "length width");
 static_assert(sizeof(decltype(Packet::crc))==2, "CRC width");
 static_assert(std::is_standard_layout<Packet>::value, "logical packet layout");
 CHECK(MaxPacketSize==radio::MaxPayload); CHECK(MaxPayloadSize==240);
 CHECK(HeaderSize==13); CHECK(PacketOverhead==15); CHECK(MaxFragmentCount==137);
 CHECK(packetSize(0)==15); CHECK(packetSize(240)==255); CHECK(crcOffset(240)==253);
 CHECK(!validMessageSize(0)); CHECK(validMessageSize(1));
 CHECK(validMessageSize(512)); CHECK(!validMessageSize(MaxMessageSize+1));
 Header h; CHECK(!valid(h)); h.payloadLength=1; CHECK(valid(h,16));
 CHECK(!valid(h,15)); CHECK(!valid(h,17));
 for(unsigned count=0;count<=255;++count)
  for(unsigned index=0;index<=255;++index)
   for(unsigned length=0;length<=255;++length) {
    h.fragmentCount=uint16_t(count); h.fragmentIndex=uint16_t(index); h.payloadLength=uint8_t(length);
    const bool expected=count>=1 && count<=MaxFragmentCount && index<count && length>=1 && length<=240 &&
     (index+1==count ? (count-1)*240+length<=MaxMessageSize : length==240);
    CHECK(valid(h)==expected);
   }
 h=Header{}; h.payloadLength=240; h.messageId=UINT16_MAX; CHECK(valid(h));
 h.version=0; CHECK(!valid(h)); h.version=Version;
 for(unsigned type=0;type<=255;++type) { h.type=static_cast<PacketType>(type); CHECK(valid(h)==(type==0)); }
 CHECK(uint8_t(PacketType::Data)==0);
 CHECK(uint8_t(PacketType::Beacon)==1);
 CHECK(uint8_t(PacketType::Control)==2);
 CHECK(CommonHeaderSize==2); CHECK(VersionOffset==0); CHECK(TypeOffset==1);
 BeaconPacket beacon; ControlPacket control; DataPacket data;
 CHECK(beacon.version==1); CHECK(beacon.type==PacketType::Beacon);
 static_assert(sizeof(decltype(BeaconPacket::timeToNextConversation))==2, "beacon countdown width");
 static_assert(sizeof(decltype(BeaconPacket::conversationDuration))==1, "beacon duration width");
 static_assert(sizeof(decltype(BeaconPacket::crc))==2, "beacon CRC width");
 CHECK(BeaconTimeToNextConversationOffset==2); CHECK(BeaconConversationDurationOffset==4);
 CHECK(BeaconCrcOffset==5); CHECK(BeaconPacketSize==7);
 CHECK(valid(beacon,7)); CHECK(!valid(beacon,6)); CHECK(!valid(beacon,8));
 CHECK(beaconCountdownMs(0)==0); CHECK(beaconCountdownMs(1)==100);
 CHECK(beaconCountdownMs(1200)==120000UL); CHECK(beaconCountdownMs(UINT16_MAX)==6553500UL);
 for(unsigned duration=0;duration<=255;++duration) {
  beacon.conversationDuration=uint8_t(duration);
  CHECK(valid(beacon)==(duration>=1 && duration<=120));
 }
 beacon.conversationDuration=10;
 for(unsigned value=0;value<=255;++value) {
  beacon.version=uint8_t(value); CHECK(valid(beacon)==(value==1));
 }
 beacon.version=1;
 for(unsigned value=0;value<=255;++value) {
  beacon.type=static_cast<PacketType>(value); CHECK(valid(beacon)==(value==1));
 }
 beacon.type=PacketType::Beacon;

 CHECK(control.version==1); CHECK(control.type==PacketType::Control);
 CHECK(valid(control,12)); CHECK(!valid(control,11)); CHECK(!valid(control,13));
 CHECK(ControlHeaderSize==10); CHECK(ControlPacketOverhead==12);
 CHECK(MaxControlBitmapCount==16); CHECK(controlPacketSize(16)==44);
 CHECK(controlCrcOffset(16)==42);
 CHECK(uint8_t(ControlFlag::None)==0); CHECK(uint8_t(ControlFlag::Syn)==1);
 CHECK(uint8_t(ControlFlag::Ack)==2); CHECK(uint8_t(ControlFlag::SynAck)==3);
 CHECK((uint8_t(ControlFlag::Syn)|uint8_t(ControlFlag::Ack))==uint8_t(ControlFlag::SynAck));
 CHECK(ControlConversationIdOffset==2); CHECK(ControlFlagsOffset==3);
 CHECK(ControlProfileChangeRequestOffset==4); CHECK(ControlWindowTimingSlideOffset==5);
 CHECK(ControlRetransmitMessageIdOffset==6); CHECK(ControlRetransmitGroupBitmapOffset==8);
 static_assert(std::is_same<decltype(ControlPacket::windowTimingSlide),int8_t>::value, "signed timing slide");
 static_assert(sizeof(decltype(ControlPacket::retransmitMessageId))==2, "control message ID width");
 static_assert(sizeof(decltype(ControlPacket::crc))==2, "control CRC width");
 for(int slide=-128;slide<=127;++slide) CHECK(windowTimingSlideMs(int8_t(slide))==slide*10);
 for(unsigned n=0;n<=255;++n) {
  control.retransmitBitmapCount=uint8_t(n);
  CHECK(valid(control)==(n==0)); CHECK(valid(control,12+2*n)==(n==0));
 }
 for(unsigned groups=0;groups<=65535;++groups) {
  control.retransmitGroupBitmap=uint16_t(groups);
  unsigned expected=0;
  for(unsigned bit=0;bit<16;++bit) if(groups & (1UL<<bit)) ++expected;
  control.retransmitBitmapCount=uint8_t(expected);
  CHECK(controlBitmapCount(uint16_t(groups))==expected);
  CHECK(valid(control,12+2*expected)); CHECK(!valid(control,13+2*expected));
 }
 control.retransmitGroupBitmap=3; control.retransmitBitmapCount=2;
 control.retransmitBitmaps[0]=uint16_t((1U<<7)|(1U<<12)|(1U<<15));
 control.retransmitBitmaps[1]=uint16_t((1U<<6)|(1U<<9));
 CHECK(valid(control,16));
 static_assert(sizeof(decltype(ControlPacket::retransmitGroupBitmap))==2, "group width");
 static_assert(sizeof(control.retransmitBitmaps[0])==2, "fragment bitmap width");
 control.retransmitGroupBitmap=0;
 control.retransmitMessageId=UINT16_MAX;
 control.retransmitBitmapCount=0;
 CHECK(valid(control));
 for(unsigned version=0;version<=255;++version) {
  control.version=uint8_t(version); CHECK(valid(control)==(version==1));
 }
 control.version=1;
 for(unsigned type=0;type<=255;++type) {
  control.type=static_cast<PacketType>(type); CHECK(valid(control)==(type==2));
 }
 control.type=PacketType::Control;
 CHECK(data.header.version==1); CHECK(data.header.type==PacketType::Data);
 static_assert(sizeof(decltype(BeaconPacket::version))==1, "beacon version width");
 static_assert(sizeof(decltype(ControlPacket::version))==1, "control version width");
 static_assert(std::is_standard_layout<BeaconPacket>::value, "beacon layout");
 static_assert(std::is_standard_layout<ControlPacket>::value, "control layout");
 static_assert(sizeof(decltype(Header::transmitterSessionId))==2, "session width");
 static_assert(sizeof(decltype(Header::retryCount))==1, "retry width");
 static_assert(sizeof(decltype(Header::flags))==1, "flags width");
 CHECK(TransmitterSessionIdOffset==2); CHECK(MessageIdOffset==4);
 CHECK(FragmentCountOffset==6); CHECK(FragmentIndexOffset==8);
 CHECK(RetryCountOffset==10); CHECK(FlagsOffset==11); CHECK(PayloadLengthOffset==12);
 h=Header{}; h.payloadLength=240; h.transmitterSessionId=UINT16_MAX;
 h.retryCount=UINT8_MAX; h.flags=UINT8_MAX; CHECK(valid(h));
 h.fragmentCount=256; CHECK(!valid(h)); h.fragmentCount=UINT16_MAX; CHECK(!valid(h));
 h.fragmentCount=1; h.fragmentIndex=256; CHECK(!valid(h));
 h.fragmentIndex=UINT16_MAX; CHECK(!valid(h));
 h.fragmentCount=3; h.fragmentIndex=2; h.payloadLength=32; CHECK(valid(h,47));
 h.fragmentCount=MaxFragmentCount; h.fragmentIndex=MaxFragmentCount-1;
 h.payloadLength=uint8_t(MaxMessageSize-(MaxFragmentCount-1)*MaxPayloadSize+1); CHECK(!valid(h));
 CHECK(CrcPolynomial==0x1021); CHECK(CrcInitial==0xFFFF); CHECK(CrcXorOut==0);
 CHECK(!CrcReflectInput && !CrcReflectOutput); CHECK(CrcCheck==0x29B1);
 Packet p; CHECK(p.crc==0); CHECK(sizeof(p.payload)==MaxPayloadSize);
 for(size_t i=0;i<MaxPayloadSize;++i) CHECK(p.payload[i]==0);
}
void managerConnections() {
 Pair p; StatisticsManager stats; stats.begin();
 ProtocolManager protocol(p.balloon,stats); protocol.begin();
 MessageManager messages(protocol,stats); LinkManager link(protocol,stats);
 struct Application : messaging::Receiver {
  unsigned count=0; uint16_t session=0, id=0; uint8_t byte=0;
  void onMessage(const messaging::MessageView& m) override {
   ++count; session=m.transmitterSessionId; id=m.messageId; byte=m.data[0];
  }
 } app;
 protocol::DataPacket data; data.header.payloadLength=1;
 data.header.transmitterSessionId=42; data.header.messageId=1000; data.payload[0]=0xAB;
 radio::PacketInfo info; info.length=16; info.rssi=-99; info.snr=2.5f;
 CHECK(protocol.acceptDecoded(data,info)==protocol::Result::NotConnected);
 messages.begin(); link.begin(); messages.setReceiver(&app);
 CHECK(protocol.acceptDecoded(data,info)==protocol::Result::Ok);
 CHECK(app.count==1); CHECK(app.session==42); CHECK(app.id==1000); CHECK(app.byte==0xAB);
 auto snapshot=link.statistics();
 CHECK(snapshot.receivedPackets==2); CHECK(snapshot.completedMessages==1);
 CHECK(snapshot.lastRssi==-99); CHECK(snapshot.lastSnr==2.5f); CHECK(snapshot.hasReceiveMeasurement);
 info.length=15;
 CHECK(protocol.acceptDecoded(data,info)==protocol::Result::InvalidArgument);
 CHECK(app.count==1); CHECK(link.statistics().rejectedPackets==1);
 protocol::ControlPacket control; info.length=12;
 CHECK(protocol.acceptDecoded(control,info)==protocol::Result::NotReady);
 protocol::BeaconPacket beacon; info.length=7;
 CHECK(protocol.acceptDecoded(beacon,info)==protocol::Result::NotReady);
 CHECK(link.sendControl(control,0)==protocol::Result::Ok);
 p.finish(p.balloon);
 CHECK(link.sendBeacon(beacon,p.channel.now)==protocol::Result::Ok);
 p.finish(p.balloon);
 CHECK(protocol.sendData(data,p.channel.now)==protocol::Result::Busy); // Stopped LinkManager closes data gate.
 messaging::MessageView view;
 CHECK(messages.send(view,0)==protocol::Result::InvalidArgument);
 CHECK(link.statistics().rejectedMessages==1);
 view.data=data.payload; view.length=1;
 CHECK(messages.send(view,0)==protocol::Result::NotReady);
 data.header.messageId++; data.header.fragmentCount=2; data.header.payloadLength=240; info.length=255;
 CHECK(protocol.acceptDecoded(data,info)==protocol::Result::NotReady);
 CHECK(app.count==1); CHECK(link.statistics().completedMessages==1);
 CHECK(link.setProfile(link_profiles::Id::P3)==Status::Ok);
 CHECK(p.balloon.config().spreadingFactor==8); CHECK(p.balloon.config().bandwidth==250000);
 CHECK(p.balloon.config().frequency==p.c.frequency);
 CHECK(link.setProfile(static_cast<link_profiles::Id>(255))==Status::InvalidConfig);
 CHECK(protocol.receive()==Status::Ok); CHECK(p.balloon.state()==State::Receiving);
 CHECK(protocol.idle()==Status::Ok);
 p.a.stuck=true;
 CHECK(p.balloon.transmit(p.payload,1,0)==Status::Ok);
 CHECK(protocol.poll(100000)==Status::Timeout);
 CHECK(protocol.poll(100001)==Status::HardwareError);
 CHECK(link.statistics().radioErrors==1);
 stats.begin(); CHECK(!link.statistics().hasReceiveMeasurement);
 CHECK(link.statistics().receivedPackets==0);
}

void receiveStatistics() {
 StatisticsManager stats; stats.begin();
 CHECK(stats.mode()==StatisticsManager::Mode::Minimal); CHECK(stats.historySize()==0);
 StatisticsManager::Observation o;
 o.integrity=StatisticsManager::Integrity::Valid; o.data=true; o.profile=0;
 o.fragmentCount=256; o.sessionId=1; o.messageId=10; o.payloadBytes=240;
 o.wireBytes=255; o.rssi=-90; o.snr=7.5f; o.receivedAtMs=1000;
 o.fragmentIndex=4; stats.observe(o);
 CHECK(stats.profile(0).pendingMissing==4); // 0..3, not an unseen tail.
 o.fragmentIndex=6; o.receivedAtMs=2000; stats.observe(o);
 CHECK(stats.pendingFragment(1,10,5)); CHECK(stats.profile(0).pendingMissing==5);
 CHECK(stats.profile(0).uniquePayloadBytes==480);
 CHECK(stats.profile(0).throughputBytesPerSecond(2000)==510);
 CHECK(stats.profile(0).goodputBytesPerSecond(2000)==480);
 o.fragmentIndex=5; o.receivedAtMs=2100; stats.observe(o);
 CHECK(!stats.pendingFragment(1,10,5)); CHECK(stats.profile(0).pendingMissing==4);
 CHECK(stats.profile(0).retryPackets==0); CHECK(stats.profile(0).uniqueFragments==3);
 stats.observe(o); CHECK(stats.profile(0).duplicateFragments==1);
 CHECK(stats.profile(0).uniquePayloadBytes==720);
 o.profile=2; o.fragmentIndex=0; o.retryCount=1; stats.observe(o);
 CHECK(stats.profile(0).pendingMissing==3); CHECK(stats.profile(2).pendingMissing==0);
 CHECK(stats.profile(2).retryPackets==1); CHECK(stats.profile(2).uniquePayloadBytes==240);
 o.integrity=StatisticsManager::Integrity::Corrupt; o.fragmentIndex=1; stats.observe(o);
 CHECK(stats.pendingFragment(1,10,1)); CHECK(stats.profile(2).corruptPackets==1);
 CHECK(stats.profile(2).corruptionRate()==0.5f);
 CHECK(stats.profile(2).uniquePayloadBytes==240);
 o.integrity=StatisticsManager::Integrity::Valid; o.fragmentIndex=1; stats.observe(o);
 CHECK(!stats.pendingFragment(1,10,1)); CHECK(stats.profile(0).pendingMissing==2);
 CHECK(stats.releaseMessage(1,10)); CHECK(stats.profile(0).pendingMissing==0);
 CHECK(!stats.releaseMessage(1,10));
 // Full 256-index reversal, duplicate detection across profile switches.
 stats.begin(); o.profile=0; o.retryCount=0;
 for(int i=255;i>=0;--i) { o.fragmentIndex=uint16_t(i); stats.observe(o); }
 CHECK(stats.profile(0).pendingMissing==0); CHECK(stats.profile(0).uniqueFragments==256);
 CHECK(stats.profile(0).provisionalMissingRate()==0);
 o.profile=4; stats.observe(o); CHECK(stats.profile(4).duplicateFragments==1);
 CHECK(stats.profile(4).uniquePayloadBytes==0);
 // Two incomplete interleaved messages are retained; third cannot inflate goodput.
 stats.begin(); o.profile=0; o.fragmentCount=256; o.fragmentIndex=6;
 o.messageId=1; stats.observe(o); o.messageId=2; stats.observe(o);
 o.messageId=3; stats.observe(o); CHECK(stats.profile(0).trackingUnavailable==1);
 CHECK(stats.profile(0).uniqueFragments==2); CHECK(stats.profile(0).pendingMissing==12);
 o.messageId=1; o.fragmentIndex=5; stats.observe(o); CHECK(stats.profile(0).pendingMissing==11);
 o.fragmentCount=255; stats.observe(o); CHECK(stats.profile(0).trackingUnavailable==2);
 // Heavy history is optional, bounded, chronological and contains no payload.
 StatisticsManager::Observation log[2], entry;
 StatisticsManager::DenseStorage dense;
 CHECK(!stats.setLogging(StatisticsManager::Mode::Heavy));
 CHECK(stats.setLogging(StatisticsManager::Mode::Heavy,log,2,&dense));
 stats.begin(); o.data=false; o.fragmentCount=0;
 o.receivedAtMs=10; stats.observe(o); o.receivedAtMs=20; stats.observe(o);
 o.receivedAtMs=30; stats.observe(o);
 CHECK(stats.historySize()==2); CHECK(stats.history(0,entry)); CHECK(entry.receivedAtMs==20);
 CHECK(stats.history(1,entry)); CHECK(entry.receivedAtMs==30); CHECK(!stats.history(2,entry));
 CHECK(stats.setLogging(StatisticsManager::Mode::Minimal)); CHECK(stats.historySize()==0);
 stats.observe(o); CHECK(stats.historySize()==0);
 // No global assumptions about unknown profiles, zero time, or clock wrap.
 stats.begin(); o.profile=200; o.receivedAtMs=UINT32_MAX-99; stats.observe(o);
 CHECK(stats.profile(StatisticsManager::UnknownProfile).packets==1);
 CHECK(stats.profile(200).throughputBytesPerSecond(o.receivedAtMs)==0);
 CHECK(stats.profile(200).throughputBytesPerSecond(100)==1275);
}

void trailingStatistics() {
 StatisticsManager stats; StatisticsManager::DenseStorage dense;
 StatisticsManager::RecentSnapshot recent;
 StatisticsManager::RecentSample sample;
 CHECK(!stats.recentProfile(0,recent));
 CHECK(stats.setDenseLogging(dense)); stats.begin();
 CHECK(stats.recentProfile(0,recent)); CHECK(recent.packets==0);
 CHECK(!stats.recentPacket(0,0,sample));
 StatisticsManager::Observation o;
 o.profile=0; o.rssi=-120; o.snr=-10; o.wireBytes=100;
 o.integrity=StatisticsManager::Integrity::Corrupt;
 for(unsigned i=0;i<100;++i) { o.receivedAtMs=i*1000; stats.observe(o); }
 o.rssi=-80; o.snr=10; o.integrity=StatisticsManager::Integrity::Valid;
 for(unsigned i=100;i<104;++i) { o.receivedAtMs=i*1000; stats.observe(o); }
 CHECK(stats.profile(0).packets==104); CHECK(stats.profile(0).meanRssi < -118);
 CHECK(stats.profile(0).corruptionRate()>0.96f);
 CHECK(stats.recentProfile(0,recent)); CHECK(recent.packets==4);
 CHECK(recent.meanRssi==-80); CHECK(recent.meanSnr==10);
 CHECK(recent.minRssi==-80); CHECK(recent.minSnr==10);
 CHECK(stats.profile(0).minRssi==-120); CHECK(stats.profile(0).minSnr==-10); CHECK(recent.corruptionRate()==0);
 CHECK(recent.firstAtMs==100000); CHECK(recent.lastAtMs==103000);
 CHECK(recent.throughputBytesPerSecond(104000)==100);
 CHECK(stats.recentPacket(0,0,sample)); CHECK(sample.observation.receivedAtMs==100000);
 CHECK(!stats.recentPacket(0,4,sample)); CHECK(stats.historySize()==0);
 o.profile=2; o.rssi=-50; stats.observe(o);
 CHECK(stats.recentProfile(2,recent)); CHECK(recent.packets==1); CHECK(recent.meanRssi==-50);
 CHECK(stats.recentProfile(0,recent)); CHECK(recent.packets==4); CHECK(recent.meanRssi==-80);
 CHECK(recent.throughputBytesPerSecond(recent.firstAtMs)==0);
 o.profile=200; stats.observe(o); CHECK(stats.recentProfile(200,recent)); CHECK(recent.packets==1);
 // Historical tracking determines useful contribution even if the original is
 // outside the trailing four samples; duplicates cannot inflate recent goodput.
 stats.begin(); o.profile=0; o.data=true; o.payloadBytes=80; o.fragmentCount=1;
 o.fragmentIndex=0; o.sessionId=1; o.messageId=1; o.retryCount=0;
 for(unsigned i=0;i<5;++i) { o.receivedAtMs=i*1000; stats.observe(o); }
 CHECK(stats.profile(0).uniquePayloadBytes==80);
 CHECK(stats.recentProfile(0,recent)); CHECK(recent.uniquePayloadBytes==0);
 CHECK(recent.duplicatePackets==4); CHECK(recent.goodputBytesPerSecond(5000)==0);
 o.messageId=2; o.retryCount=1; o.receivedAtMs=5000; stats.observe(o);
 CHECK(stats.recentProfile(0,recent)); CHECK(recent.uniquePayloadBytes==80);
 CHECK(recent.retryPackets==1); CHECK(recent.duplicatePackets==3);
 CHECK(stats.profile(0).uniquePayloadBytes==160);
 CHECK(stats.setLogging(StatisticsManager::Mode::Minimal));
 CHECK(!stats.recentProfile(0,recent)); CHECK(stats.profile(0).packets==6);
 // Re-enable dense mode with empty recent state, retaining historical means.
 CHECK(stats.setDenseLogging(dense)); CHECK(stats.recentProfile(0,recent)); CHECK(recent.packets==0);
 CHECK(stats.profile(0).packets==6);
 stats.begin(); CHECK(stats.profile(0).packets==0); CHECK(stats.mode()==StatisticsManager::Mode::Heavy);
 CHECK(stats.recentProfile(0,recent)); CHECK(recent.packets==0);
}

void signalMinimums() {
 StatisticsManager stats; StatisticsManager::DenseStorage dense;
 CHECK(stats.setDenseLogging(dense)); stats.begin();
 StatisticsManager::Observation o; o.profile=1; o.rssi=-70; o.snr=8;
 stats.observe(o); CHECK(stats.profile(1).minSnr==8); CHECK(stats.profile(1).minRssi==-70);
 o.rssi=-100; o.snr=-5; stats.observe(o);
 CHECK(stats.profile(1).meanRssi==-85); CHECK(stats.profile(1).meanSnr==1.5f);
 CHECK(stats.profile(1).minRssi==-100); CHECK(stats.profile(1).minSnr==-5);
 o.rssi=-60; o.snr=12;
 for(unsigned i=0;i<4;++i) stats.observe(o);
 StatisticsManager::RecentSnapshot recent;
 CHECK(stats.recentProfile(1,recent)); CHECK(recent.minRssi==-60); CHECK(recent.minSnr==12);
 CHECK(stats.profile(1).minRssi==-100); CHECK(stats.profile(1).minSnr==-5);
 o.profile=2; o.rssi=-40; o.snr=15; stats.observe(o);
 CHECK(stats.profile(2).minRssi==-40); CHECK(stats.profile(2).minSnr==15);
 CHECK(stats.profile(1).minRssi==-100);
 stats.begin(); o.profile=1; o.rssi=-50; o.snr=20; stats.observe(o);
 CHECK(stats.profile(1).minRssi==-50); CHECK(stats.profile(1).minSnr==20);
 CHECK(stats.recentProfile(1,recent)); CHECK(recent.minSnr==20);
}

void decodedFragments() {
 Pair p; StatisticsManager stats; stats.begin();
 ProtocolManager protocol(p.ground,stats); protocol.begin();
 struct Consumer : protocol::DataReceiver {
  unsigned received=0; protocol::Fragment saved;
  protocol::Result onFragment(const protocol::Fragment& f) override {
   ++received; saved=f; return protocol::Result::Ok;
  }
 } consumer;
 struct LinkConsumer : protocol::LinkReceiver {
  unsigned beacons=0, controls=0;
  int8_t slide=0; uint8_t flags=0; uint16_t message=0, bitmap=0;
  protocol::Result onControl(const protocol::ControlPacket& c,const radio::PacketInfo&) override {
   ++controls; flags=c.flags; slide=c.windowTimingSlide; message=c.retransmitMessageId; bitmap=c.retransmitBitmaps[0];
   return protocol::Result::Ok;
  }
  protocol::Result onBeacon(const protocol::BeaconPacket& b,const radio::PacketInfo&) override {
   ++beacons; CHECK(b.timeToNextConversation==1200); return protocol::Result::Ok;
  }
 } link;
 protocol.setDataReceiver(&consumer); protocol.setLinkReceiver(&link);
 uint8_t bytes[255]={1,0,0x12,0x34,0xAB,0xCD,0,1,0,0,2,0xA5,1,0xEF};
 auto checksum=[&](size_t n) {
  const uint16_t crc=protocol::crc16(bytes,n-2); bytes[n-2]=uint8_t(crc>>8); bytes[n-1]=uint8_t(crc);
 };
 const uint8_t vector[]={'1','2','3','4','5','6','7','8','9'};
 CHECK(protocol::crc16(vector,9)==0x29B1); checksum(16);
 CHECK(p.balloon.transmit(bytes,16,p.channel.now)==Status::Ok); p.finish(p.balloon);
 CHECK(protocol.poll(1000)==Status::Ok); CHECK(consumer.received==1);
 CHECK(consumer.saved.header.transmitterSessionId==0x1234);
 CHECK(consumer.saved.header.messageId==0xABCD); CHECK(consumer.saved.header.retryCount==2);
 CHECK(consumer.saved.header.flags==0xA5); CHECK(consumer.saved.payloadLength()==1);
 CHECK(consumer.saved.payload()[0]==0xEF); CHECK(consumer.saved.rawLength==16);
 CHECK(consumer.saved.reception.rssi==-90); CHECK(consumer.saved.reception.snr==7.5f);
 CHECK(consumer.saved.reception.receivedAtMs==1000); CHECK(consumer.saved.reception.hasModulation);
 CHECK(stats.profile(0).uniquePayloadBytes==1); CHECK(stats.profile(0).retryPackets==1);
 CHECK(stats.profile(0).crcCheckedPackets==1);
 radio::PacketInfo info=consumer.saved.reception;
 bytes[13]^=1;
 CHECK(protocol.receiveRaw(bytes,16,info)==protocol::Result::InvalidArgument);
 CHECK(consumer.received==1); CHECK(stats.profile(0).corruptPackets==1);
 CHECK(stats.profile(0).uniquePayloadBytes==1);
 checksum(16); bytes[0]=9; checksum(16);
 CHECK(protocol.receiveRaw(bytes,16,info)==protocol::Result::InvalidArgument);
 CHECK(stats.profile(0).malformedPackets==1); CHECK(stats.profile(0).crcCheckedPackets==3);
 bytes[0]=1;
 // Fuzz lengths/truncation with fresh CRCs: malformed frames must never overread.
 for(size_t n=0;n<=255;++n) {
  for(size_t i=0;i<16;++i) bytes[i]=consumer.saved.rawBytes[i];
  info.length=n;
  if(n>=4) checksum(n);
  protocol.receiveRaw(bytes,n,info);
 }
 CHECK(consumer.received==2); // Only n=16 is a valid data packet.
 // Valid beacon, then valid control with signed timing and two-level bitmaps.
 bytes[0]=1; bytes[1]=1; bytes[2]=4; bytes[3]=0xB0; bytes[4]=10;
 checksum(7); info.length=7;
 CHECK(protocol.receiveRaw(bytes,7,info)==protocol::Result::Ok); CHECK(link.beacons==1);
 bytes[1]=2; bytes[2]=1; bytes[3]=uint8_t(protocol::ControlFlag::SynAck); bytes[4]=2; bytes[5]=0xFE;
 bytes[6]=0xAB; bytes[7]=0xCD; bytes[8]=0; bytes[9]=1; bytes[10]=0x90; bytes[11]=0x80;
 checksum(14); info.length=14;
 CHECK(protocol.receiveRaw(bytes,14,info)==protocol::Result::Ok);
 CHECK(link.controls==1); CHECK(link.flags==uint8_t(protocol::ControlFlag::SynAck)); CHECK(link.slide==-2); CHECK(link.message==0xABCD); CHECK(link.bitmap==0x9080);
 // Hardware CRC failures have measurements, but never trusted data identities.
 stats.begin(); p.b.pending.assign(bytes,bytes+14); p.b.measurement.length=14; p.b.crcError=true;
 CHECK(protocol.poll(2000)==Status::CrcError);
 CHECK(stats.profile(0).corruptPackets==1); CHECK(stats.profile(0).uniqueFragments==0);
 CHECK(stats.profile(0).wireBytes==14); CHECK(stats.profile(0).corruptionRate()==1);
 // Link profile classification reflects captured receive settings, not policy in radio.
 const link_profiles::Definition definitions[]={link_profiles::P0,link_profiles::P1,
  link_profiles::P2,link_profiles::P3,link_profiles::P4};
 for(const auto& def:definitions) {
  CHECK(link_profiles::identify(def.spreadingFactor,def.bandwidth,def.codingRate)==uint8_t(def.id));
 }
 CHECK(link_profiles::identify(7,125000,5)==StatisticsManager::UnknownProfile);
 Config changed=p.c; changed.spreadingFactor=8; changed.bandwidth=250000; changed.codingRate=5;
 CHECK(p.balloon.configure(changed)==Status::Ok); CHECK(p.ground.configure(changed)==Status::Ok);
 CHECK(p.balloon.transmit(bytes,14,p.channel.now)==Status::Ok); p.finish(p.balloon);
 CHECK(protocol.poll(3000)==Status::Ok);
 CHECK(stats.profile(3).packets==1); CHECK(stats.profile(0).packets==1);
 CHECK(link.controls==2);

}

struct LinkPair {
 Channel channel; FakeRadioAdapter a{channel},b{channel};
 RadioManager balloonRadio{a},groundRadio{b};
 StatisticsManager bs,gs;
 ProtocolManager bp{balloonRadio,bs},gp{groundRadio,gs};
 LinkManager balloon{bp,bs},ground{gp,gs};
 LinkManager::Config config;
 bool dropAck=false, dropSynAck=false, dropAll=false;
 LinkPair(uint32_t start=0) {
  channel.now=start; CHECK(balloonRadio.initialize(Config{})==Status::Ok);
  CHECK(groundRadio.initialize(Config{})==Status::Ok);
  bs.begin(); gs.begin(); bp.begin(); gp.begin(); balloon.begin(); ground.begin();
  config.autoProbe=false;
  CHECK(balloon.start(LinkManager::Role::Balloon,config,start)==protocol::Result::Ok);
  CHECK(ground.start(LinkManager::Role::Ground,config,start)==protocol::Result::Ok);
 }
 void step() {
  channel.drop=dropAll || (dropAck && b.mode==State::Transmitting && b.sending.size()>=4 && b.sending[1]==2 && b.sending[3]==2)
   || (dropSynAck && a.mode==State::Transmitting && a.sending.size()>=4 && a.sending[1]==2 && a.sending[3]==3);
  channel.advance(10); bp.poll(channel.now); gp.poll(channel.now);
  balloon.poll(channel.now); ground.poll(channel.now);
 }
 void advance(uint32_t ms) { for(uint32_t i=0;i<ms;i+=10) step(); }
};
void linkAcquisition() {
 LinkPair p;
 CHECK(p.balloon.phase()==LinkManager::Phase::Data); CHECK(p.ground.phase()==LinkManager::Phase::Searching);
 CHECK(p.balloon.dataProfile()==link_profiles::Id::P0);
 CHECK(p.ground.requestDataProfile(link_profiles::Id::P1)==protocol::Result::Ok);
 p.advance(30000); CHECK(p.balloon.phase()==LinkManager::Phase::Beacon);
 p.advance(20); CHECK(p.a.mode!=State::Transmitting); // Quiet guard.
 p.advance(1980); CHECK(p.ground.phase()==LinkManager::Phase::Data);
 CHECK(p.gs.profile(0).packets>0); // Real encoded beacons traversed the fake channel.
 p.advance(8000); CHECK(p.balloon.phase()==LinkManager::Phase::Data);
 CHECK(p.balloon.acquiring());
 p.advance(30000); CHECK(p.balloon.phase()==LinkManager::Phase::Control);
 p.advance(6000);
 CHECK(p.balloon.established()); CHECK(p.ground.established());
 CHECK(p.balloon.conversation()==LinkManager::Conversation::Complete);
 CHECK(p.ground.conversation()==LinkManager::Conversation::Provisional);
 CHECK(p.balloon.dataProfile()==link_profiles::Id::P1); CHECK(p.ground.dataProfile()==link_profiles::Id::P1);
 CHECK(!p.balloon.pendingAgreement()); CHECK(!p.ground.pendingAgreement());
 CHECK(p.balloonRadio.config().spreadingFactor==12); CHECK(p.groundRadio.config().spreadingFactor==12);
 p.advance(5000); CHECK(p.balloon.phase()==LinkManager::Phase::Data);
 CHECK(p.balloonRadio.config().spreadingFactor==10); CHECK(p.groundRadio.config().spreadingFactor==10);
 CHECK(p.balloon.conversationId()==0);
 // Two silent listening windows reset data to P0; four trigger acquisition.
 p.dropAll=true; p.advance(80000);
 CHECK(p.balloon.dataProfile()==link_profiles::Id::P0); CHECK(p.ground.dataProfile()==link_profiles::Id::P0);
 CHECK(p.balloon.missedControlWindows()==2);
 p.advance(80000); CHECK(p.balloon.acquiring()); CHECK(!p.balloon.established());
 CHECK(p.ground.phase()==LinkManager::Phase::Searching);
}
void beaconResynchronization() {
 // A valid acquisition beacon overrides an established schedule, including
 // an active control conversation. Invalid beacons must leave it intact.
 for(unsigned control=0;control<2;++control) {
  LinkPair p(UINT32_MAX-90000);
  if(control) CHECK(p.ground.requestDataProfile(link_profiles::Id::P1)==protocol::Result::Ok);
  p.advance(81000);
  CHECK(p.ground.established());
  if(control) {
   p.advance(29000);
   CHECK(p.ground.phase()==LinkManager::Phase::Control);
   CHECK(p.ground.dataProfile()==link_profiles::Id::P1);
   p.dropSynAck=true;
   for(unsigned i=0;i<500 && p.ground.conversation()!=LinkManager::Conversation::WaitSynAck;++i) p.step();
   CHECK(p.ground.conversation()==LinkManager::Conversation::WaitSynAck);
   CHECK(p.ground.pendingAgreement());
  }
  protocol::BeaconPacket beacon;
  beacon.timeToNextConversation=123;
  beacon.conversationDuration=8;
  radio::PacketInfo info;
  info.length=protocol::BeaconPacketSize; info.receivedAtMs=p.channel.now;
  info.hasModulation=true; info.spreadingFactor=12;info.bandwidth=125000;info.codingRate=8;
  const auto oldEnd=p.ground.phaseEndsAtMs();
  auto invalid=beacon;invalid.timeToNextConversation=0;
  CHECK(p.ground.onBeacon(invalid,info)==protocol::Result::InvalidArgument);
  CHECK(p.ground.established());CHECK(p.ground.phaseEndsAtMs()==oldEnd);
  auto wrong=info;wrong.spreadingFactor=7;
  CHECK(p.ground.onBeacon(beacon,wrong)==protocol::Result::InvalidArgument);
  CHECK(p.ground.established());
  CHECK(p.ground.onBeacon(beacon,info)==protocol::Result::Ok);
  CHECK(!p.ground.established()); CHECK(p.ground.acquiring());
  CHECK(p.ground.phase()==LinkManager::Phase::Data);
  CHECK(p.ground.conversation()==LinkManager::Conversation::Idle);
  CHECK(!p.ground.pendingAgreement()); CHECK(!p.ground.probing());
  CHECK(p.ground.dataProfile()==link_profiles::Id::P0);
  CHECK(p.ground.phaseEndsAtMs()==info.receivedAtMs+12300);
  // Subsequent beacons refine the boundary, including across millis wrap.
  info.receivedAtMs+=1000;beacon.timeToNextConversation=114;
  CHECK(p.ground.onBeacon(beacon,info)==protocol::Result::Ok);
  CHECK(p.ground.phaseEndsAtMs()==info.receivedAtMs+11400);
  CHECK(p.ground.onBeacon(beacon,info)==protocol::Result::Ok);
  CHECK(p.ground.phaseEndsAtMs()==info.receivedAtMs+11400);
  p.ground.poll(p.ground.phaseEndsAtMs());
  CHECK(p.ground.phase()==LinkManager::Phase::Control);
  CHECK(p.ground.phaseEndsAtMs()==info.receivedAtMs+11400+8000);
 }
 // Real encoded RF packets after a balloon-only restart must clear the old
 // ground agreement on the first received beacon, then complete a new handshake.
 LinkPair p;p.advance(85000);
 CHECK(p.ground.established());CHECK(p.balloon.established());
 CHECK(p.balloon.start(LinkManager::Role::Balloon,p.config,p.channel.now)==protocol::Result::Ok);
 bool reacquiring=false;
 for(unsigned elapsed=0;elapsed<42000;elapsed+=10) {
  p.step();
  if(p.ground.acquiring()) { reacquiring=true;break; }
 }
 CHECK(reacquiring); CHECK(!p.ground.established());
 CHECK(p.ground.missedControlWindows()<4);
 p.advance(50000);
 CHECK(p.balloon.established());CHECK(p.ground.established());
 printf("PASS: established/control beacon override and balloon-only restart recovery\n");
}
void lostHandshake() {
 LinkPair p;
 p.ground.requestDataProfile(link_profiles::Id::P1); p.dropAck=true;
 p.advance(74000);
 CHECK(p.balloon.pendingAgreement()); CHECK(p.balloon.dataProfile()==link_profiles::Id::P0);
 p.advance(5000);
 CHECK(!p.balloon.established()); CHECK(p.balloon.dataProfile()==link_profiles::Id::P0);
 CHECK(!p.balloon.pendingAgreement()); // ACK timeout discards the balloon's proposal.
 CHECK(p.ground.conversation()==LinkManager::Conversation::Provisional);
 CHECK(p.ground.dataProfile()==link_profiles::Id::P1); // Explicit final-ACK uncertainty, not false agreement.
 p.advance(2000); CHECK(p.balloonRadio.config().spreadingFactor==12);
 p.dropAck=false;
 // Since balloon never established, next acquisition slot is a beacon; then listen.
 p.advance(80000); CHECK(p.balloon.established()); CHECK(p.balloon.dataProfile()==link_profiles::Id::P1);
 LinkPair q; q.dropSynAck=true; auto faster=q.config; faster.replyTimeoutMs=2000;
 CHECK(q.ground.start(LinkManager::Role::Ground,faster,q.channel.now)==protocol::Result::Ok);
 q.ground.requestDataProfile(link_profiles::Id::P2);
 q.advance(79000);
 CHECK(q.ground.conversationId()>1); CHECK(q.balloon.dataProfile()==link_profiles::Id::P0);
 CHECK(q.ground.dataProfile()==link_profiles::Id::P0);
 q.advance(2000); CHECK(!q.balloon.pendingAgreement()); CHECK(!q.ground.pendingAgreement());
}
void retransmissionHandshake() {
 struct Cache : LinkManager::RetransmissionHandler {
  unsigned commits=0, discarded=0; bool available=true;
  void discard(const protocol::ControlPacket&) override { ++discarded; }
  bool canRetransmit(const protocol::ControlPacket&) override { return available; }
  void retransmit(const protocol::ControlPacket& request) override {
   ++commits; CHECK(request.retransmitMessageId==1000); CHECK(request.retransmitGroupBitmap==3);
  }
 } cache;
 LinkPair p; p.balloon.setRetransmissionHandler(&cache);
 uint16_t maps[]={0x9080,0x0240};
 CHECK(p.ground.requestRetransmission(1000,3,maps,2)==protocol::Result::Ok);
 p.advance(78000); CHECK(cache.commits==1);
 // Deliver a duplicate ACK through actual CRC decoding; no second replay.
 const auto wire=p.b.sending;
 radio::PacketInfo info; info.length=wire.size(); info.receivedAtMs=p.channel.now;
 info.hasModulation=true; info.spreadingFactor=12; info.bandwidth=125000; info.codingRate=8;
 CHECK(p.bp.receiveRaw(wire.data(),wire.size(),info)==protocol::Result::Ok);
 CHECK(cache.commits==1);
 const auto response=p.a.sending;
 info.length=response.size();
 CHECK(p.gp.receiveRaw(response.data(),response.size(),info)==protocol::Result::Ok);
 p.advance(1800);
 CHECK(p.ground.conversation()==LinkManager::Conversation::Provisional);
 CHECK(cache.commits==1);
 p.advance(1200); cache.available=false;
 p.advance(40000); CHECK(cache.commits==1); CHECK(p.ground.retransmissionRefused()); // Nonzero request is refused, not fictitiously queued.
 LinkPair q; q.balloon.setRetransmissionHandler(&cache); cache.available=true;
 q.dropAck=true; q.ground.requestRetransmission(1000,3,maps,2); q.advance(81000);
 CHECK(cache.commits==1); CHECK(cache.discarded==1); // Lost final ACK discards reservations, not messages.
}
void linkClockAndBoundaries() {
 LinkPair p(UINT32_MAX-10000);
 p.advance(81000); CHECK(p.balloon.established()); CHECK(p.ground.established());
 protocol::DataPacket data; data.header.payloadLength=240;
 // Request that cannot fit before the data-window deadline must wait.
 LinkPair q; q.advance(29990);
 CHECK(q.bp.sendData(data,q.channel.now)==protocol::Result::Busy);
 q.advance(20); CHECK(q.bp.sendData(data,q.channel.now)==protocol::Result::Busy);
 LinkManager::Config invalid=q.config; invalid.dataWindowMs=0;
 CHECK(q.balloon.start(LinkManager::Role::Balloon,invalid,q.channel.now)==protocol::Result::InvalidArgument);
 // Ground never follows inbound ground-originated timing; balloon rejects nonzero ground slide.
}

void outboundPackets() {
 Pair p; StatisticsManager stats; ProtocolManager tx(p.balloon,stats),rx(p.ground,stats);
 struct Consumer : protocol::DataReceiver {
  unsigned calls=0; protocol::Fragment fragment;
  protocol::Result onFragment(const protocol::Fragment& f) override { ++calls; fragment=f; return protocol::Result::Ok; }
 } consumer;
 rx.setDataReceiver(&consumer);
 protocol::DataPacket data; data.header.payloadLength=240; data.header.messageId=0x4321;
 data.header.transmitterSessionId=0xABCD; data.header.flags=0x80;
 for(unsigned i=0;i<240;++i) data.payload[i]=uint8_t(i);
 CHECK(tx.sendData(data,p.channel.now)==protocol::Result::Ok);
 CHECK(tx.sendBeacon(protocol::BeaconPacket{},p.channel.now)==protocol::Result::Busy);
 p.finish(p.balloon); CHECK(rx.poll(p.channel.now)==Status::Ok);
 CHECK(consumer.calls==1); CHECK(consumer.fragment.header.messageId==0x4321);
 CHECK(consumer.fragment.header.transmitterSessionId==0xABCD);
 CHECK(consumer.fragment.rawLength==255);
 for(unsigned i=0;i<240;++i) CHECK(consumer.fragment.payload()[i]==uint8_t(i));
 CHECK(stats.snapshot().receivedPackets==1); // Outbound packet creates no receive statistics.
}
void airtimeGoodput() {
 StatisticsManager stats; StatisticsManager::DenseStorage dense; stats.begin();stats.setDenseLogging(dense);
 StatisticsManager::Observation o; o.profile=1;o.data=true;o.integrity=StatisticsManager::Integrity::Valid;
 o.fragmentCount=2;o.payloadBytes=100;o.wireBytes=115;o.airtimeMs=100;o.receivedAtMs=10;
 stats.observe(o);o.fragmentIndex=1;o.receivedAtMs=20;stats.observe(o);
 StatisticsManager::AirtimeSnapshot a;
 CHECK(stats.recentAirtimeProfile(1,0,a));CHECK(a.goodputBytesPerSecond()==1000);
 o.retryCount=1;o.receivedAtMs=30;stats.observe(o); // Duplicate costs airtime, no useful bytes.
 o.integrity=StatisticsManager::Integrity::Corrupt;o.data=false;o.receivedAtMs=40;stats.observe(o);
 CHECK(stats.recentAirtimeProfile(1,0,a));CHECK(a.goodputBytesPerSecond()==500);CHECK(a.corruptPackets==1);
 o.integrity=StatisticsManager::Integrity::Valid;o.airtimeMs=1000;
 for(unsigned i=0;i<20;++i) stats.observe(o); // Valid control traffic cannot evict data.
 CHECK(stats.recentAirtimeProfile(1,0,a));CHECK(a.packets==4);CHECK(a.goodputBytesPerSecond()==500);
 // Same useful bytes and samples, but elapsed time includes idle gaps.
 CHECK(a.airtimeGoodputBytesPerSecond()==500);
 const uint32_t wallElapsed=1000-a.firstAtMs;
 CHECK(a.wallGoodputBytesPerSecond(1000)==float(200)*1000/wallElapsed);
 CHECK(a.wallGoodputBytesPerSecond(2000)<a.wallGoodputBytesPerSecond(1000));
 CHECK(a.airtimeGoodputBytesPerSecond()==500); // Idle does not change airtime rate.
 CHECK(stats.recentAirtimeProfile(1,31,a));CHECK(a.packets==1);CHECK(a.goodputBytesPerSecond()==0);
 o.data=true;o.fragmentCount=1;o.fragmentIndex=0;o.retryCount=0;o.airtimeMs=100;
 for(unsigned i=0;i<20;++i) { o.messageId=i+1;o.receivedAtMs=50+i;stats.observe(o); }
 CHECK(stats.recentAirtimeProfile(1,0,a));CHECK(a.packets==16);CHECK(a.goodputBytesPerSecond()==1000);
 stats.begin();CHECK(stats.recentAirtimeProfile(1,0,a));CHECK(a.packets==0);CHECK(a.wallGoodputBytesPerSecond(1000)==0);CHECK(a.airtimeGoodputBytesPerSecond()==0);
}
void probeNegotiationPersistence() {
 for(unsigned failures : {1U,2U,3U}) {
  LinkPair p;StatisticsManager::DenseStorage dense;p.gs.setDenseLogging(dense);
  auto c=p.config;c.autoProbe=true;
  CHECK(p.ground.start(LinkManager::Role::Ground,c,0)==protocol::Result::Ok);
  p.advance(81000);CHECK(p.ground.established());
  StatisticsManager::Observation o;o.integrity=StatisticsManager::Integrity::Valid;
  o.data=true;o.profile=0;o.fragmentCount=4;o.payloadBytes=100;o.airtimeMs=1000;
  for(unsigned i=0;i<4;++i) { o.fragmentIndex=i;o.receivedAtMs=p.channel.now;p.gs.observe(o); }
  p.dropSynAck=true;
  unsigned attempts=0;
  for(unsigned window=0;window<failures;++window) {
   for(unsigned ms=0;ms<40000;ms+=10) {
    p.step();if(p.ground.conversationId()>attempts) attempts=p.ground.conversationId();
   }
   CHECK(p.ground.dataProfile()==link_profiles::Id::P0);
   CHECK(p.ground.failedProbeWindows()==window+1);
   CHECK(p.ground.negotiatingProbe()==(window<2));
  }
  CHECK(attempts>1); // Retry within the same P0 control window.
  p.dropSynAck=false;
  p.advance(40000);
  if(failures<3) {
   CHECK(p.ground.dataProfile()==link_profiles::Id::P1);
   CHECK(p.balloon.dataProfile()==link_profiles::Id::P1);
   CHECK(p.ground.probing());CHECK(!p.ground.negotiatingProbe());CHECK(p.ground.failedProbeWindows()==0);
  } else {
   CHECK(!p.ground.probing());CHECK(p.ground.dataProfile()==link_profiles::Id::P0);
  }
 }
}
void evidenceUpgrades() {
 CHECK(LinkManager::Config{}.upwardProbeIntervalMs==16UL*60*1000);
 for(unsigned change : {0U,1U,2U,3U}) {
  LinkPair p;StatisticsManager::DenseStorage dense;p.gs.setDenseLogging(dense);
  auto c=p.config;c.autoProbe=true;
  CHECK(p.ground.start(LinkManager::Role::Ground,c,0)==protocol::Result::Ok);
  p.advance(81000);uint16_t message=0;
  auto feed=[&](uint8_t profile,uint8_t payload,float snr) {
   StatisticsManager::Observation o;o.integrity=StatisticsManager::Integrity::Valid;
   o.profile=profile;o.data=true;o.fragmentCount=1;o.payloadBytes=payload;o.airtimeMs=1000;o.snr=snr;
   for(unsigned i=0;i<4;++i) { o.messageId=++message;o.receivedAtMs=p.channel.now;p.gs.observe(o); }
   p.advance(40000);
  };
  feed(0,100,0);CHECK(p.ground.dataProfile()==link_profiles::Id::P1);
  feed(1,200,0);CHECK(p.ground.dataProfile()==link_profiles::Id::P2);
  feed(2,150,0);CHECK(p.ground.dataProfile()==link_profiles::Id::P1);
  CHECK(p.ground.peakAirtimeGoodput(link_profiles::Id::P1)==200);
  if(!change || change==3) {
   for(unsigned i=0;i<8;++i) { feed(1,change==3 ? 170 : 200,change==3 ? 4 : 0);CHECK(p.ground.dataProfile()==link_profiles::Id::P1);CHECK(!p.ground.probing()); }
  } else {
   // Better evidence alone cannot bypass the cooldown or sustained-window gate.
   for(unsigned i=0;i<3;++i) {
    feed(1,change==2 ? 230 : 200,change==1 ? 4 : 0);
    CHECK(p.ground.dataProfile()==(i<2 ? link_profiles::Id::P1 : link_profiles::Id::P2));
   }
   CHECK(p.ground.probeReason()==LinkManager::ProbeReason::Evidence);
   feed(2,250,4);CHECK(p.ground.dataProfile()==link_profiles::Id::P2);
   CHECK(!p.ground.probing()); // Evidence upgrades evaluate one step, not a blind sweep.
  }
 }
}
void silentDataRecovery() {
 LinkPair p;StatisticsManager::DenseStorage dense;p.gs.setDenseLogging(dense);
 auto c=p.config;c.autoProbe=true;
 CHECK(p.ground.start(LinkManager::Role::Ground,c,0)==protocol::Result::Ok);p.advance(81000);
 uint16_t message=0;
 auto feed=[&](uint8_t profile,uint8_t payload) {
  StatisticsManager::Observation o;o.integrity=StatisticsManager::Integrity::Valid;
  o.profile=profile;o.data=true;o.fragmentCount=1;o.payloadBytes=payload;o.airtimeMs=500;
  for(unsigned i=0;i<4;++i) { o.messageId=++message;o.receivedAtMs=p.channel.now;p.gs.observe(o); }
  p.advance(40000);
 };
 feed(0,50);feed(1,100);feed(2,150);feed(3,200);feed(4,190);
 CHECK(p.ground.dataProfile()==link_profiles::Id::P3); // Failed P4 trial returned to P3.
 for(unsigned i=1;i<=2;++i) { p.advance(40000);CHECK(p.ground.dataProfile()==link_profiles::Id::P3);CHECK(p.ground.silentDataWindows()==i); }
 feed(3,200);CHECK(p.ground.silentDataWindows()==0);CHECK(p.ground.dataProfile()==link_profiles::Id::P3);
 for(unsigned i=1;i<=3;++i) {
  p.advance(40000);
  CHECK(p.ground.dataProfile()==(i<3 ? link_profiles::Id::P3 : link_profiles::Id::P0));
  CHECK(p.balloon.dataProfile()==p.ground.dataProfile());
  CHECK(p.ground.missedControlWindows()==0);
 }
}
void profileInstanceRecords() {
 StatisticsManager stats;StatisticsManager::DenseStorage dense;stats.begin();stats.setDenseLogging(dense);
 CHECK(!stats.profileRecord(3));CHECK(!stats.profileRecord(3,2));
 stats.beginDataWindow(3,0,10000);
 StatisticsManager::Observation o;o.integrity=StatisticsManager::Integrity::Valid;
 o.data=true;o.profile=3;o.fragmentCount=1;o.payloadBytes=100;o.wireBytes=115;o.airtimeMs=100;
 for(unsigned i=1;i<=6;++i) {
  o.messageId=i;o.receivedAtMs=i*1000;o.rssi=-100+int(i)*10;o.snr=i;stats.observe(o);
 }
 auto* record=stats.profileRecord(3);CHECK(record && record->active);const auto firstId=record->instanceId;
 auto whole=record->whole(6000),recent=record->recent(6000);
 CHECK(record->sampleCount==4);CHECK(whole.packets.packets==6);CHECK(recent.packets.packets==4);
 CHECK(whole.packets.meanRssi==-65);CHECK(recent.packets.meanRssi==-55);
 CHECK(whole.packets.meanSnr==3.5f);CHECK(recent.packets.meanSnr==4.5f);
 CHECK(whole.wallGoodputBytesPerSecond()==100);CHECK(whole.airtimeGoodputBytesPerSecond()==1000);
 CHECK(recent.wallGoodputBytesPerSecond()==float(400)*1000/3100);
 CHECK(recent.airtimeGoodputBytesPerSecond()==1000);
 CHECK(record->recent(7000).wallGoodputBytesPerSecond()<recent.wallGoodputBytesPerSecond());
 stats.recordReliableGoodput(3,1000);stats.endDataWindow(10000);
 // Control packets must neither rotate the data-profile record nor evict its four samples.
 o.data=false;o.profile=0;o.receivedAtMs=11000;stats.observe(o);
 CHECK(stats.profileRecord(3)->instanceId==firstId);CHECK(!stats.profileRecord(0));
 CHECK(stats.profileRecord(3)->total.packets.packets==6);
 stats.beginDataWindow(3,20000,30000);CHECK(stats.profileRecord(3)->instanceId==firstId);
 o.data=true;o.profile=3;o.messageId=7;o.payloadBytes=200;o.receivedAtMs=21000;stats.observe(o);
 CHECK(stats.profileRecord(3)->total.packets.uniquePayloadBytes==800);
 CHECK(stats.profileRecord(3)->whole(21000).airtimeGoodputBytesPerSecond()==float(800)*1000/700);
 CHECK(stats.profileRecord(3)->recent(21000).airtimeGoodputBytesPerSecond()==1250);
 stats.beginDataWindow(4,30000,40000);
 CHECK(!stats.profileRecord(3)->active);CHECK(!stats.profileRecord(3,1));
 CHECK(stats.profileRecord(3)->whole(99999).wallMs==30000);
 CHECK(stats.profileRecord(3)->recent(99999).wallMs==26100);
 stats.beginDataWindow(3,50000,60000);
 record=stats.profileRecord(3);const auto secondId=record->instanceId;
 CHECK(record->active);CHECK(secondId!=firstId);CHECK(record->total.packets.packets==0);
 CHECK(stats.profileRecord(3,1)->instanceId==firstId);
 CHECK(stats.profileRecord(3,1)->total.packets.uniquePayloadBytes==800);
 CHECK(stats.profile(3).uniquePayloadBytes==0);CHECK(stats.peakAirtimeGoodput(3)==1000);
 // Duplicate identity survives the visit boundary; no fictional new useful delivery.
 o.receivedAtMs=51000;stats.observe(o);
 CHECK(record->total.packets.duplicateFragments==1);CHECK(record->total.packets.uniquePayloadBytes==0);
 o.messageId=8;o.receivedAtMs=52000;stats.observe(o);
 CHECK(record->total.packets.uniquePayloadBytes==200);
 o.integrity=StatisticsManager::Integrity::Corrupt;o.data=false;o.receivedAtMs=53000;stats.observe(o);
 CHECK(record->total.packets.corruptPackets==1);
 CHECK(record->total.airtimeMs==300);CHECK(record->total.packets.uniquePayloadBytes==200);
 CHECK(stats.profileRecord(3,1)->total.packets.uniquePayloadBytes==800);
 stats.recordReliableGoodput(3,500);stats.beginDataWindow(4,60000,70000);
 stats.beginDataWindow(3,80000,90000);
 CHECK(stats.profileRecord(3,1)->instanceId==secondId);CHECK(!stats.profileRecord(3,2));
 CHECK(stats.peakAirtimeGoodput(3)==500); // Evicted oldest visit no longer contributes a peak.
 const auto thirdId=stats.profileRecord(3)->instanceId;
 for(unsigned i=0;i<100;++i) {
  stats.beginDataWindow(3,80000+i*10000,90000+i*10000);
  o.integrity=StatisticsManager::Integrity::Valid;o.data=true;o.payloadBytes=100;
  o.messageId=100+i;o.receivedAtMs=81000+i*10000;stats.observe(o);
  stats.endDataWindow(90000+i*10000);
 }
 record=stats.profileRecord(3);CHECK(record->instanceId==thirdId);
 CHECK(record->total.dataWindows==100); // Reopening identical bounds is idempotent.
 CHECK(record->sampleCount==4);CHECK(record->total.packets.packets==100);
 CHECK(record->total.packets.uniquePayloadBytes==10000);CHECK(record->recent(1080000).packets.packets==4);
 stats.endProfileInstance(1080000);CHECK(!stats.profileRecord(3)->active);
 stats.begin();CHECK(!stats.profileRecord(3));CHECK(!stats.profileRecord(3,1));
 // Millisecond wrap does not split an instance or corrupt its rate denominator.
 const uint32_t start=UINT32_MAX-1000;stats.beginDataWindow(2,start,start+5000);
 o.profile=2;o.messageId=1;o.receivedAtMs=start+500;o.airtimeMs=100;stats.observe(o);
 stats.endProfileInstance(start+5000);record=stats.profileRecord(2);
 CHECK(record->whole(0).wallMs==5000);CHECK(record->recent(0).wallMs==4600);
}
void longTermDegradation() {
 for(unsigned scenario : {0U,1U,2U,3U}) {
  LinkPair p;StatisticsManager::DenseStorage dense;p.gs.setDenseLogging(dense);
  auto c=p.config;c.autoProbe=true;
  CHECK(p.ground.start(LinkManager::Role::Ground,c,0)==protocol::Result::Ok);p.advance(81000);
  uint16_t message=0;
  auto feed=[&](uint8_t profile,uint8_t payload) {
   StatisticsManager::Observation o;o.integrity=StatisticsManager::Integrity::Valid;
   o.profile=profile;o.data=true;o.fragmentCount=1;o.payloadBytes=payload;o.airtimeMs=1000;
   for(unsigned i=0;i<4;++i) { o.messageId=++message;o.receivedAtMs=p.channel.now;p.gs.observe(o); }
   p.advance(40000);
  };
  feed(0,50);feed(1,100);feed(2,90);
  CHECK(p.ground.dataProfile()==link_profiles::Id::P1);
  // Returning to P1 begins a new instance; the previous P1 average cannot
  // falsely label this fresh visit as degraded.
  if(scenario==3) {
   feed(1,40);CHECK(p.ground.dataProfile()==link_profiles::Id::P1);
   CHECK(p.ground.degradedDataWindows()==0);
   continue;
  }
  for(unsigned i=0;i<10;++i) feed(1,100);
  CHECK(p.ground.dataProfile()==link_profiles::Id::P1);
  const auto* record=p.gs.profileRecord(1);
  CHECK(record->whole(p.channel.now).airtimeGoodputBytesPerSecond()==100);
  if(scenario==1) {
   feed(1,60);CHECK(p.ground.degradedDataWindows()==1);
   feed(1,60);CHECK(p.ground.degradedDataWindows()==2);
   feed(1,100);CHECK(p.ground.degradedDataWindows()==0);
   for(unsigned i=1;i<=3;++i) {
    feed(1,60);CHECK(p.ground.dataProfile()==(i<3 ? link_profiles::Id::P1 : link_profiles::Id::P0));
   }
  } else {
   const uint8_t values[]={90,81,73,66,59,53};
   for(unsigned i=0;i<6;++i) {
    if(scenario==2 && i==5) p.dropSynAck=true;
    feed(1,values[i]);
    CHECK(p.ground.dataProfile()==(i<5 || scenario==2 ? link_profiles::Id::P1 : link_profiles::Id::P0));
    if(i==3) CHECK(p.ground.degradedDataWindows()==1);
    if(i==4) CHECK(p.ground.degradedDataWindows()==2);
   }
   // Every adjacent score fell less than 25%; the cumulative reference still
   // detected the persistent decline. A failed downgrade handshake is retried.
   if(scenario==2) {
    CHECK(p.ground.degradedDataWindows()==3);p.dropSynAck=false;
    feed(1,100);CHECK(p.ground.dataProfile()==link_profiles::Id::P0);
   }
  }
  CHECK(p.balloon.dataProfile()==link_profiles::Id::P0);CHECK(p.ground.degradedDataWindows()==0);
 }
}
void profileProbePolicy() {
 longTermDegradation();
 profileInstanceRecords();
 silentDataRecovery();
 evidenceUpgrades();
 probeNegotiationPersistence();
 airtimeGoodput();
 LinkPair p; StatisticsManager::DenseStorage dense; p.gs.setDenseLogging(dense); auto automatic=p.config; automatic.autoProbe=true; automatic.upwardProbeIntervalMs=120000;
 CHECK(p.ground.start(LinkManager::Role::Ground,automatic,0)==protocol::Result::Ok);
 p.advance(81000); CHECK(p.ground.established());
 StatisticsManager::Observation o; o.integrity=StatisticsManager::Integrity::Valid;
 o.data=true; o.profile=0; o.fragmentCount=4; o.wireBytes=255; o.payloadBytes=240; o.airtimeMs=1000;
 o.sessionId=1; o.messageId=1;
 for(unsigned i=0;i<2;++i) { o.fragmentIndex=i; o.receivedAtMs=p.channel.now; p.gs.observe(o); }
 p.advance(40000); CHECK(p.ground.dataProfile()==link_profiles::Id::P0); // Two packets are insufficient.
 for(unsigned i=2;i<4;++i) { o.fragmentIndex=i; o.receivedAtMs=p.channel.now; p.gs.observe(o); }
 p.advance(40000); CHECK(p.ground.dataProfile()==link_profiles::Id::P1);
 CHECK(p.balloon.dataProfile()==link_profiles::Id::P1);
 for(unsigned window=1;window<=3;++window) {
  p.advance(40000); // Healthy P0 control exchanges do not hide silent data windows.
  CHECK(p.ground.dataProfile()==(window<3 ? link_profiles::Id::P1 : link_profiles::Id::P0));
  CHECK(p.balloon.dataProfile()==p.ground.dataProfile());
 }
 CHECK(p.balloon.missedControlWindows()==0);
 // A failed startup probe must wait for the elapsed-time cooldown, then retry.
 for(unsigned window=0;window<3;++window) {
  for(unsigned i=0;i<4;++i) {
   o.fragmentCount=1;o.fragmentIndex=0;o.messageId=uint16_t(10+window*4+i);
   o.receivedAtMs=p.channel.now;p.gs.observe(o);
  }
  p.advance(40000);
  CHECK(p.ground.dataProfile()==(window<2 ? link_profiles::Id::P0 : link_profiles::Id::P1));
 }
 // A periodic probe climbs again when airtime-normalized useful delivery improves.
 o.profile=1;o.airtimeMs=200;
 for(unsigned i=0;i<4;++i) { o.messageId=100+i;o.receivedAtMs=p.channel.now;p.gs.observe(o); }
 p.advance(40000);CHECK(p.ground.dataProfile()==link_profiles::Id::P2);
 // An equally fast candidate provides no improvement and returns to P1.
 o.profile=2;
 for(unsigned i=0;i<4;++i) { o.messageId=110+i;o.receivedAtMs=p.channel.now;p.gs.observe(o); }
 p.advance(40000);CHECK(p.ground.dataProfile()==link_profiles::Id::P1);

}

void transactionValidation() {
 struct Source : LinkManager::RetransmissionHandler {
  unsigned reserved=0, committed=0, discarded=0;
  bool canRetransmit(const protocol::ControlPacket&) override { ++reserved; return true; }
  void retransmit(const protocol::ControlPacket&) override { ++committed; }
  void discard(const protocol::ControlPacket&) override { ++discarded; }
 } source;
 LinkPair p; p.dropAck=true; p.balloon.setRetransmissionHandler(&source);
 uint16_t maps[]={0x9080,0x0240}; p.ground.requestRetransmission(10,3,maps,2);
 p.ground.requestDataProfile(link_profiles::Id::P1); p.advance(74000);
 CHECK(p.balloon.pendingAgreement()); CHECK(source.reserved==1);
 CHECK(!p.balloon.setRetransmissionHandler(nullptr));
 const auto ack=p.b.sending; auto changed=ack;
 radio::PacketInfo info; info.length=changed.size(); info.receivedAtMs=p.channel.now;
 info.hasModulation=true; info.spreadingFactor=12; info.bandwidth=125000; info.codingRate=8;
 auto crc=[&]() { const uint16_t value=protocol::crc16(changed.data(),changed.size()-2);
  changed[changed.size()-2]=uint8_t(value>>8); changed.back()=uint8_t(value); };
 changed[4]=2; crc(); // Valid CRC, but wrong proposed profile.
 CHECK(p.bp.receiveRaw(changed.data(),changed.size(),info)==protocol::Result::InvalidArgument);
 CHECK(p.balloon.pendingAgreement()); CHECK(p.balloon.dataProfile()==link_profiles::Id::P0);
 changed=ack; changed[3]=0; crc();
 CHECK(p.bp.receiveRaw(changed.data(),changed.size(),info)==protocol::Result::InvalidArgument);
 changed=ack; changed[2]=2; crc();
 CHECK(p.bp.receiveRaw(changed.data(),changed.size(),info)==protocol::Result::InvalidArgument);
 changed=ack; changed[3]=1; crc(); // Identical duplicate SYN reuses the reservation.
 CHECK(p.bp.receiveRaw(changed.data(),changed.size(),info)==protocol::Result::Ok);
 CHECK(source.reserved==1); CHECK(source.committed==0);
 p.advance(7000); CHECK(source.discarded==1); CHECK(source.committed==0);
 CHECK(p.balloon.dataProfile()==link_profiles::Id::P0);
 // Delayed ACK from the expired attempt cannot commit a proposal in the data phase.
 info.receivedAtMs=p.channel.now;
 CHECK(p.bp.receiveRaw(ack.data(),ack.size(),info)==protocol::Result::InvalidArgument);
 CHECK(source.committed==0);
}

void expiredAckDeadline() {
 LinkPair p; p.dropAck=true; p.ground.requestDataProfile(link_profiles::Id::P1);
 p.advance(74000); CHECK(p.balloon.pendingAgreement());
 const auto ack=p.b.sending;
 p.channel.advance(5000); // Delayed servicing: timeout must be enforced by receive too.
 radio::PacketInfo info; info.length=ack.size(); info.receivedAtMs=p.channel.now;
 info.hasModulation=true; info.spreadingFactor=12; info.bandwidth=125000; info.codingRate=8;
 CHECK(p.bp.receiveRaw(ack.data(),ack.size(),info)==protocol::Result::InvalidArgument);
 CHECK(!p.balloon.pendingAgreement()); CHECK(p.balloon.dataProfile()==link_profiles::Id::P0);
}

void telemetryWindowTest() {
 const uint16_t pauses[]={2000,1000,500,250,125};
 const link_profiles::Definition profiles[]={link_profiles::P0,link_profiles::P1,link_profiles::P2,link_profiles::P3,link_profiles::P4};
 for(unsigned i=0;i<5;++i) {
  auto c=Config{};c.spreadingFactor=profiles[i].spreadingFactor;c.bandwidth=profiles[i].bandwidth;c.codingRate=profiles[i].codingRate;
  CHECK(telemetry::pauseMs(c)==pauses[i]);
 }
 LinkPair p;MessageManager::Storage storage;
 MessageManager sender(p.bp,p.bs,storage),receiver(p.gp,p.gs);sender.begin(123);receiver.begin();
 struct Sink : messaging::Receiver {
  unsigned count=0;uint16_t previous=0;bool first=true;
  void onMessage(const messaging::MessageView& m) override {
   CHECK(m.transmitterSessionId==123);if(!first)CHECK(m.messageId==uint16_t(previous+1));
   previous=m.messageId;first=false;++count;
  }
 } sink;
 receiver.setReceiver(&sink);sender.setInterPacketPause(2000);
 messaging::SensorData data;data.externalRtd=true;data.externalRtdCentiC=-3000;
 unsigned accepted=0;
 for(unsigned elapsed=0;elapsed<180000;elapsed+=10) {
  p.step();sender.poll(p.channel.now);receiver.poll(p.channel.now);
  if(p.balloon.phase()==LinkManager::Phase::Data && sender.ready(p.channel.now)) {
   CHECK(sender.sendSensors(data,p.channel.now)==protocol::Result::Ok);++accepted;
  }
 }
 p.advance(5000);CHECK(accepted>8);CHECK(sink.count>4);
 CHECK(p.balloon.established());CHECK(p.ground.established());
 printf("Typed telemetry windows: queued=%u received=%u; handshake preserved\n",accepted,sink.count);
}
void integratedCounterTest() {
 LinkPair p; MessageManager messages(p.gp,p.gs); messages.begin();
 link_test::CounterReceiver receiver; messages.setReceiver(&receiver);
 link_test::CounterSource source(p.bp,30);
 uint32_t preLink=0, postLink=0, balloonEpoch=p.a.epoch, dataTransmissions=0;
 bool sawBeacon=false, sawControl=false, sawComplete=false;
 for(uint32_t elapsed=0;elapsed<180000;elapsed+=10) {
  const uint32_t before=receiver.received();
  p.step();
  if(receiver.received()!=before) {
   if(p.ground.established()) ++postLink; else ++preLink;
  }
  if(p.balloon.phase()==LinkManager::Phase::Beacon) sawBeacon=true;
  if(p.balloon.phase()==LinkManager::Phase::Control) sawControl=true;
  if(p.balloon.conversation()==LinkManager::Conversation::Complete) sawComplete=true;
  if(p.balloon.phase()==LinkManager::Phase::Data && p.balloonRadio.state()==State::Idle)
   source.trySend(p.channel.now);
  if(p.a.epoch!=balloonEpoch && p.a.mode==State::Transmitting) {
   balloonEpoch=p.a.epoch;
   if(p.a.sending.size()>1 && p.a.sending[1]==0) {
    ++dataTransmissions; CHECK(p.balloon.phase()==LinkManager::Phase::Data);
    CHECK(p.a.settings.spreadingFactor==12); CHECK(p.a.settings.bandwidth==125000);
    CHECK(p.a.settings.codingRate==8);
   }
  }
 }
 CHECK(source.finished()); CHECK(source.sent()==30); CHECK(dataTransmissions==30);
 CHECK(receiver.received()==30); CHECK(receiver.last()==29);
 CHECK(receiver.invalid()==0); CHECK(receiver.sequenceErrors()==0);
 CHECK(preLink>0); CHECK(postLink>0); CHECK(sawBeacon); CHECK(sawControl); CHECK(sawComplete);
 CHECK(p.balloon.established()); CHECK(p.ground.established());
 CHECK(p.balloon.dataProfile()==link_profiles::Id::P0); CHECK(p.ground.dataProfile()==link_profiles::Id::P0);
 CHECK(p.gs.profile(0).dataPackets==30); CHECK(p.gs.profile(0).uniquePayloadBytes==240);
 CHECK(p.gs.profile(0).corruptPackets==0); CHECK(p.gs.snapshot().completedMessages==30);
 CHECK(p.gs.profile(0).lastRssi==-90); CHECK(p.gs.profile(0).lastSnr==7.5f);
 printf("Integrated P0 simulation: TX=%lu RX=%lu pre-link=%lu post-link=%lu last=29 invalid=0 sequenceErrors=0 usefulBytes=240\n",
  (unsigned long)source.sent(),(unsigned long)receiver.received(),(unsigned long)preLink,(unsigned long)postLink);
}
#include "MessageTests.h"
#include "PressureTests.h"
#include "RestartTests.h"
#ifdef PIO_UNIT_TESTING
#include <unity.h>
void setUp() {}
void tearDown() {}
int main() {
 restartPacketTests(); pressureTelemetryTests(); messageManagerTests(); UNITY_BEGIN(); RUN_TEST(telemetryWindowTest); RUN_TEST(integratedCounterTest); RUN_TEST(expiredAckDeadline); RUN_TEST(transactionValidation); RUN_TEST(outboundPackets); RUN_TEST(profileProbePolicy); RUN_TEST(beaconResynchronization); RUN_TEST(linkAcquisition); RUN_TEST(lostHandshake); RUN_TEST(retransmissionHandshake); RUN_TEST(linkClockAndBoundaries); RUN_TEST(signalMinimums); RUN_TEST(trailingStatistics); RUN_TEST(receiveStatistics); RUN_TEST(decodedFragments); RUN_TEST(managerConnections); RUN_TEST(protocolStructure); RUN_TEST(initialization); RUN_TEST(packets); RUN_TEST(bidirectional); RUN_TEST(profiles); RUN_TEST(errorsAndTiming); RUN_TEST(repeated); return UNITY_END(); }
#else
int main() { restartPacketTests(); pressureTelemetryTests(); messageManagerTests(); telemetryWindowTest(); integratedCounterTest(); expiredAckDeadline(); transactionValidation(); outboundPackets(); profileProbePolicy(); beaconResynchronization(); linkAcquisition(); lostHandshake(); retransmissionHandshake(); linkClockAndBoundaries(); signalMinimums(); trailingStatistics(); receiveStatistics(); decodedFragments(); managerConnections(); protocolStructure(); initialization(); packets(); bidirectional(); profiles(); errorsAndTiming(); repeated(); printf("PASS: %u checks; 1000 alternating transmissions\n",checks); }
#endif
