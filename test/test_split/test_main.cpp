#include "Simulation.h"
#include <RadioServer.h>
#include <RemoteProtocol.h>
#include <LinkManager.h>
#include <MessageManager.h>
#include <deque>
#include <vector>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <new>
using namespace board_link;
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1); } } while(0)
struct World {
 uint64_t us=0;simulation::Channel rf;std::vector<std::function<void()>> tasks;
 void step() { const auto before=us/1000;us+=100;if(us/1000!=before) rf.advance(uint32_t(us/1000-before));for(auto& f:tasks) f(); }
 void advance(uint32_t ms) { const auto end=us+uint64_t(ms)*1000;while(us<end) step(); }
};
struct SerialIO : IO {
 World& world;SerialIO* peer=nullptr;std::deque<uint8_t> rx;std::deque<std::pair<uint64_t,uint8_t>> tx;
 uint32_t offset=0;uint64_t last=0;bool disconnected=false;size_t capacity=64;int corruptNext=-1;unsigned overrun=0;
 explicit SerialIO(World& w,uint32_t o=0):world(w),offset(o) {}
 int readByte() override { if(rx.empty()) return -1;const auto x=rx.front();rx.pop_front();return x; }
 bool writeByte(uint8_t x) override {
  if(tx.size()>=capacity) return false;
  last=std::max(last,world.us)+(10000000ULL+Baud-1)/Baud;
  if(corruptNext==0) { x^=0x40;corruptNext=-1; }else if(corruptNext>0) --corruptNext;
  tx.push_back({last,x});return true;
 }
 void deliver() {
  while(!tx.empty() && tx.front().first<=world.us) {
   if(!disconnected && !peer->disconnected) { if(peer->rx.size()==peer->capacity) ++peer->overrun;else peer->rx.push_back(tx.front().second); }
   tx.pop_front();
  }
 }
 uint32_t now() const override { return uint32_t(world.us/1000)+offset; }
 void progress() override { world.step(); }
};
struct Device {
 World& world;SerialIO hostIO,radioIO;simulation::FakeRadioAdapter adapter;radio::RadioManager radio;RadioServer server;
 StatisticsManager stats;RemoteProtocol remote;bool polling=false, runServer=true;
 explicit Device(World& w,uint32_t hostOffset=0,uint32_t radioOffset=70000):world(w),hostIO(w,hostOffset),radioIO(w,radioOffset),adapter(w.rf),radio(adapter),server(radioIO,radio),remote(hostIO,stats) {
  hostIO.peer=&radioIO;radioIO.peer=&hostIO;stats.begin();server.begin();
  w.tasks.push_back([this] {
   hostIO.deliver();radioIO.deliver();remote.pump();server.pump();
   if(!polling && runServer) { polling=true;server.poll();polling=false; }
  });
 }
 void begin(uint32_t session=123) { CHECK(remote.connect(session));CHECK(remote.initialize(radio::Config{})==radio::Status::Ok); }
 void pollFor(uint32_t ms) { const uint32_t end=hostIO.now()+ms;while(int32_t(hostIO.now()-end)<0) { remote.poll();world.advance(1); } }
};
struct Capture : protocol::DataReceiver, protocol::LinkReceiver {
 unsigned data=0,control=0,beacons=0;protocol::Fragment packet;radio::PacketInfo info;
 protocol::Result onFragment(const protocol::Fragment& p) override { ++data;packet=p;info=p.reception;return protocol::Result::Ok; }
 protocol::Result onControl(const protocol::ControlPacket&,const radio::PacketInfo& p) override { ++control;info=p;return protocol::Result::Ok; }
 protocol::Result onBeacon(const protocol::BeaconPacket&,const radio::PacketInfo& p) override { ++beacons;info=p;return protocol::Result::Ok; }
};
void transportAndRadio() {
 World w;Device a(w);a.begin();CHECK(a.remote.radioState()==radio::State::Idle);
 CHECK(a.remote.receive()==radio::Status::Ok);CHECK(a.radio.state()==radio::State::Receiving);
 auto c=a.remote.radioConfig();c.spreadingFactor=7;c.bandwidth=500000;c.codingRate=5;
 CHECK(a.remote.configureRadio(c)==radio::Status::Ok);CHECK(a.adapter.settings.spreadingFactor==7);
 c.spreadingFactor=0;CHECK(a.remote.configureRadio(c)==radio::Status::InvalidConfig);CHECK(a.remote.radioConfig().spreadingFactor==7);
 protocol::DataPacket p;p.header.payloadLength=240;
 for(unsigned i=0;i<240;++i) p.payload[i]=uint8_t(i); // Includes escape/delimiter bytes.
 a.remote.allowDataTransmission(false);CHECK(a.remote.sendData(p,a.hostIO.now())==protocol::Result::Busy);
 a.remote.setDataWindow(a.hostIO.now()+1);CHECK(a.remote.sendData(p,a.hostIO.now())==protocol::Result::Busy);
 a.remote.setDataWindow(a.hostIO.now()+2000);CHECK(a.remote.sendData(p,a.hostIO.now())==protocol::Result::Ok);
 CHECK(a.remote.transmission()==radio::TxStatus::InProgress);
 CHECK(a.remote.idle()==radio::Status::Busy);a.pollFor(200);CHECK(a.remote.transmission()==radio::TxStatus::Complete);
 CHECK(a.hostIO.overrun==0 && a.radioIO.overrun==0);CHECK(a.remote.badFrames==0 && a.server.badFrames==0);
 // A corrupted request times out, does not execute, then framing recovers.
 const auto commands=a.server.commands;a.hostIO.corruptNext=4;
 CHECK(a.remote.receive()==radio::Status::HardwareError);CHECK(!a.remote.connected());CHECK(a.server.commands==commands);
 CHECK(a.remote.lastTimeoutOp==uint8_t(Op::Receive));
 CHECK(uint32_t(a.hostIO.now()-a.remote.lastTimeoutAtMs)<5);
 CHECK(a.remote.maxCommandRttMs>0);
 const auto timeoutStamp=a.remote.lastTimeoutAtMs;
 CHECK(a.remote.connect(456));CHECK(a.remote.receive()==radio::Status::Ok);
 CHECK(a.remote.lastTimeoutOp==uint8_t(Op::Receive) && a.remote.lastTimeoutAtMs==timeoutStamp);
 a.pollFor(100);a.hostIO.disconnected=true;w.advance(1600);CHECK(!a.server.connected());CHECK(a.radio.state()==radio::State::Idle);
 a.hostIO.disconnected=false;CHECK(a.remote.connect(789));CHECK(a.remote.idle()==radio::Status::Ok);
 printf("PASS: byte-paced UART, escaping, backpressure, radio failures, deadlines, timeout and lease\n");
}
struct ActivityCapture : ActivityReceiver {
 std::vector<PacketActivity> packets;
 void onPacketActivity(const PacketActivity& p) override { packets.push_back(p); }
};
void packetPipeline() {
 World w;Device a(w);a.begin();StatisticsManager::DenseStorage dense;a.stats.setDenseLogging(dense);ActivityCapture activity;a.remote.setActivityReceiver(&activity);Capture cap;a.remote.setDataReceiver(&cap);a.remote.setLinkReceiver(&cap);
 simulation::FakeRadioAdapter peerAdapter(w.rf);radio::RadioManager peerRadio(peerAdapter);StatisticsManager peerStats;ProtocolManager peer(peerRadio,peerStats);peer.begin();
 auto c=radio::Config{};c.spreadingFactor=7;c.bandwidth=500000;c.codingRate=5;
 CHECK(peerRadio.initialize(c)==radio::Status::Ok);CHECK(a.remote.configureRadio(c)==radio::Status::Ok);CHECK(a.remote.receive()==radio::Status::Ok);
 a.stats.beginDataWindow(4,a.hostIO.now(),a.hostIO.now()+10000);
 protocol::DataPacket p;p.header.payloadLength=240;for(unsigned i=0;i<240;++i)p.payload[i]=uint8_t(i);
 CHECK(peer.sendData(p,w.rf.now)==protocol::Result::Ok);a.pollFor(300);
 CHECK(activity.packets.size()==1);CHECK(!activity.packets.back().transmitting);
 CHECK(activity.packets.back().type==protocol::PacketType::Data);CHECK(activity.packets.back().profile==4);
 CHECK(activity.packets.back().durationMs==radio::RadioManager::airtimeMs(c,255));
 CHECK(uint32_t(a.hostIO.now()-(activity.packets.back().startMs+activity.packets.back().durationMs))<300);
 CHECK(cap.data==1);CHECK(cap.packet.rawLength==255);CHECK(cap.packet.profile==4);CHECK(cap.packet.header.payloadLength==240);
 for(unsigned i=0;i<240;++i)CHECK(cap.packet.payload()[i]==uint8_t(i));
 CHECK(cap.info.rssi==-90 && cap.info.snr==7.5f);CHECK(cap.info.spreadingFactor==7);
 CHECK(uint32_t(a.hostIO.now()-cap.info.receivedAtMs)<300);CHECK(a.stats.profile(4).packets==1);CHECK(a.stats.profile(4).uniquePayloadBytes==240);
 StatisticsManager::AirtimeSnapshot airtime;
 CHECK(a.stats.recentAirtimeProfile(4,0,airtime));CHECK(airtime.airtimeMs==radio::RadioManager::airtimeMs(c,255));
 CHECK(airtime.uniquePayloadBytes==240);
 const auto* record=a.stats.profileRecord(4);CHECK(record && record->active);
 CHECK(record->total.airtimeMs==radio::RadioManager::airtimeMs(c,255));
 CHECK(record->recent(a.hostIO.now()).packets.meanSnr==7.5f);
 CHECK(record->whole(a.hostIO.now()).packets.uniquePayloadBytes==240);
 CHECK(peer.poll(w.rf.now)==radio::Status::Ok);CHECK(peer.sendData(p,w.rf.now)==protocol::Result::Ok);a.adapter.crcError=true;a.pollFor(300);
 CHECK(cap.data==1);CHECK(a.stats.profile(4).corruptPackets==1);CHECK(activity.packets.size()==1);
 CHECK(a.stats.recentAirtimeProfile(4,0,airtime));CHECK(airtime.airtimeMs==2*radio::RadioManager::airtimeMs(c,255));CHECK(airtime.uniquePayloadBytes==240);
 CHECK(peer.poll(w.rf.now)==radio::Status::Ok);
 uint8_t malformed[4]={1,99,0,0};auto crc=protocol::crc16(malformed,2);malformed[2]=uint8_t(crc>>8);malformed[3]=uint8_t(crc);
 CHECK(peerRadio.transmit(malformed,4,w.rf.now)==radio::Status::Ok);a.pollFor(200);CHECK(a.stats.profile(4).malformedPackets==1);
 CHECK(peerRadio.poll(w.rf.now)==radio::Status::Ok);protocol::ControlPacket control;control.flags=1;control.conversationId=2;
 control.retransmitGroupBitmap=0xffff;control.retransmitBitmapCount=16;for(auto& x:control.retransmitBitmaps)x=0x7e7d;
 CHECK(peer.sendControl(control,w.rf.now)==protocol::Result::Ok);a.pollFor(200);CHECK(cap.control==1);CHECK(activity.packets.back().type==protocol::PacketType::Control);CHECK(activity.packets.back().flags==1);
 CHECK(peer.poll(w.rf.now)==radio::Status::Ok);protocol::BeaconPacket beacon;beacon.timeToNextConversation=300;
 CHECK(peer.sendBeacon(beacon,w.rf.now)==protocol::Result::Ok);a.pollFor(200);CHECK(cap.beacons==1);
 CHECK(a.remote.overflows==0 && a.remote.badFrames==0);CHECK(a.hostIO.overrun==0 && a.radioIO.overrun==0);
 const auto count=activity.packets.size();
 CHECK(a.remote.sendControl(control,a.hostIO.now())==protocol::Result::Ok);
 CHECK(activity.packets.size()==count+1);CHECK(activity.packets.back().transmitting);
 CHECK(activity.packets.back().flags==control.flags);
 CHECK(uint32_t(a.hostIO.now()-activity.packets.back().startMs)<100);
 CHECK(a.remote.sendControl(control,a.hostIO.now())==protocol::Result::Busy);
 CHECK(activity.packets.size()==count+1); // No phantom event on rejected TX.
 printf("PASS: real packet codecs through simulated RF and UART, max payload/control, integrity and statistics\n");
}
struct MessageCapture : messaging::Receiver {
 unsigned count=0;bool valid=true;
 void onMessage(const messaging::MessageView& m) override {
  ++count;valid=valid && m.length==240 && m.transmitterSessionId==0x1234;
  for(size_t i=0;i<m.length;++i) valid=valid && m.data[i]==uint8_t(i);
 }
};
void diagnosticsAndRestart() {
 World w;w.us=(uint64_t(UINT32_MAX)-200)*1000;w.rf.now=uint32_t(w.us/1000);
 Device d(w,0,0);d.begin();MessageManager messages(d.remote,d.stats);messages.begin();MessageCapture capture;messages.setReceiver(&capture);
 CHECK(d.remote.diagnostic(0)==protocol::Result::Ok);d.pollFor(100);CHECK(capture.count==1 && capture.valid);
 CHECK(d.remote.diagnostic(1)==protocol::Result::InvalidArgument);d.pollFor(100);
 CHECK(d.remote.diagnostic(2)==protocol::Result::InvalidArgument);d.pollFor(100);
 CHECK(d.stats.profile(0).packets==3);CHECK(d.stats.profile(0).corruptPackets==1);CHECK(d.stats.profile(0).malformedPackets==1);
 CHECK(d.stats.snapshot().completedMessages==1);
 // Reboot only the radio MCU while preserving the host's prior session/state.
 d.server.~RadioServer();d.radio.~RadioManager();new(&d.radio) radio::RadioManager(d.adapter);new(&d.server) RadioServer(d.radioIO,d.radio);d.server.begin();
 CHECK(d.remote.receive()==radio::Status::HardwareError);CHECK(!d.remote.connected());
 d.begin(999);CHECK(d.remote.receive()==radio::Status::Ok);d.pollFor(300);CHECK(d.remote.connected());
 // Drop one observation through a UART CRC failure: the following event's
 // sequence gap must invalidate the service, including an outstanding RPC.
 d.radioIO.corruptNext=4;
 CHECK(d.remote.diagnostic(0)==protocol::Result::HardwareError);
 CHECK(!d.remote.connected());CHECK(d.remote.radioState()==radio::State::Fault);
 CHECK(d.remote.eventGaps>=1);CHECK(d.remote.badFrames>=1);
 d.begin(1000);d.pollFor(50);CHECK(d.remote.connected());
 CHECK(d.hostIO.overrun==0 && d.radioIO.overrun==0);
 printf("PASS: diagnostic decoder roundtrip, MessageManager delivery, millis wrap and independent radio reboot\n");
}

void acquisition() {
 World w;Device a(w,0,100000),b(w,40000,900000);a.begin(1);b.begin(2);
 LinkManager balloon(a.remote,a.stats),ground(b.remote,b.stats);balloon.begin();ground.begin();
 LinkManager::Config c;c.autoProbe=false;
 CHECK(balloon.start(LinkManager::Role::Balloon,c,a.hostIO.now())==protocol::Result::Ok);
 CHECK(ground.start(LinkManager::Role::Ground,c,b.hostIO.now())==protocol::Result::Ok);
 const auto until=w.us+120000000;
 while(w.us<until && (!balloon.established() || !ground.established())) {
  a.remote.poll();b.remote.poll();balloon.poll(a.hostIO.now());ground.poll(b.hostIO.now());w.advance(1);
 }
 if(!balloon.established() || !ground.established()) fprintf(stderr,"link states balloon=%u/%u ground=%u/%u time=%llu\n",unsigned(balloon.phase()),unsigned(balloon.conversation()),unsigned(ground.phase()),unsigned(ground.conversation()),w.us/1000);
 CHECK(balloon.established());CHECK(ground.established());CHECK(a.remote.timeouts==0 && b.remote.timeouts==0);
 CHECK(a.hostIO.overrun==0 && b.hostIO.overrun==0 && a.radioIO.overrun==0 && b.radioIO.overrun==0);
 const auto dataUntil=w.us+20000000;
 while(w.us<dataUntil && (balloon.phase()!=LinkManager::Phase::Data || ground.phase()!=LinkManager::Phase::Data)) {
  a.remote.poll();b.remote.poll();balloon.poll(a.hostIO.now());ground.poll(b.hostIO.now());w.advance(1);
 }
 CHECK(ground.requestDataProfile(link_profiles::Id::P1)==protocol::Result::Ok);
 const auto profileUntil=w.us+100000000;
 while(w.us<profileUntil && (balloon.dataProfile()!=link_profiles::Id::P1 || ground.dataProfile()!=link_profiles::Id::P1 || balloon.phase()!=LinkManager::Phase::Data || ground.phase()!=LinkManager::Phase::Data)) {
  a.remote.poll();b.remote.poll();balloon.poll(a.hostIO.now());ground.poll(b.hostIO.now());w.advance(1);
 }
 CHECK(balloon.dataProfile()==link_profiles::Id::P1 && ground.dataProfile()==link_profiles::Id::P1);
 CHECK(a.adapter.settings.spreadingFactor==10 && b.adapter.settings.spreadingFactor==10);
 CHECK(a.remote.eventGaps==0 && b.remote.eventGaps==0);
 printf("PASS: two split devices acquire and complete SYN/SYN-ACK/ACK with four independent clocks (%llu ms)\n",w.us/1000);
}
void multipartMessage() {
 World w;Device a(w),b(w);a.begin(11);b.begin(22);
 auto config=radio::Config{};config.spreadingFactor=7;config.bandwidth=500000;config.codingRate=5;
 CHECK(a.remote.configureRadio(config)==radio::Status::Ok);CHECK(b.remote.configureRadio(config)==radio::Status::Ok);
 CHECK(b.remote.receive()==radio::Status::Ok);
 MessageManager::Storage txStorage,rxStorage;
 MessageManager tx(a.remote,a.stats,txStorage),rx(b.remote,b.stats,rxStorage);
 tx.begin(123);rx.begin();
 struct Sink : messaging::Receiver {
  unsigned count=0;
  void onMessage(const messaging::MessageView& m) override {
   CHECK(m.length==1000 && m.transmitterSessionId==123 && m.messageId==0);
   for(size_t i=0;i<m.length;++i)CHECK(m.data[i]==uint8_t(i+1));
   ++count;
  }
 } sink;
 rx.setReceiver(&sink);uint8_t payload[1000];for(size_t i=0;i<sizeof(payload);++i)payload[i]=uint8_t(i+1);
 messaging::MessageView message;message.data=payload;message.length=sizeof(payload);
 a.remote.allowDataTransmission(false);
 CHECK(tx.send(message,a.hostIO.now())==protocol::Result::Ok);CHECK(tx.busy());
 for(unsigned i=0;i<100;++i) {a.remote.poll();b.remote.poll();tx.poll(a.hostIO.now());rx.poll(b.hostIO.now());w.advance(1);}
 CHECK(sink.count==0);a.remote.allowDataTransmission(true);
 for(unsigned i=0;i<10000 && !sink.count;++i) {
  a.remote.poll();b.remote.poll();tx.poll(a.hostIO.now());rx.poll(b.hostIO.now());w.advance(1);
 }
 CHECK(!tx.busy() && sink.count==1);
 CHECK(a.remote.timeouts==0 && b.remote.timeouts==0 && a.remote.eventGaps==0 && b.remote.eventGaps==0);
 CHECK(a.hostIO.overrun==0 && b.hostIO.overrun==0 && a.radioIO.overrun==0 && b.radioIO.overrun==0);
 printf("PASS: 1000-byte MessageManager transfer through two RF/UART split devices, queued behind data gate\n");
}
int main() { multipartMessage();transportAndRadio();packetPipeline();diagnosticsAndRestart();acquisition();printf("PASS split: %u checks\n",checks); }
