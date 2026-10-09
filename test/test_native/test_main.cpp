#include "Simulation.h"
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
#ifdef PIO_UNIT_TESTING
#include <unity.h>
void setUp() {}
void tearDown() {}
int main() { UNITY_BEGIN(); RUN_TEST(initialization); RUN_TEST(packets); RUN_TEST(bidirectional); RUN_TEST(profiles); RUN_TEST(errorsAndTiming); RUN_TEST(repeated); return UNITY_END(); }
#else
int main() { initialization(); packets(); bidirectional(); profiles(); errorsAndTiming(); repeated(); printf("PASS: %u checks; 1000 alternating transmissions\n",checks); }
#endif
