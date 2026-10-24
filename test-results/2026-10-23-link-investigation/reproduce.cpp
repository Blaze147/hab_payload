// Investigative harness; reuses the production split-device simulation.
#define main original_split_test_main
#include "../../test/test_split/test_main.cpp"
#undef main

void observe(bool injectFaults) {
 World w;Device a(w),b(w);a.begin(11);b.begin(22);
 LinkManager balloon(a.remote,a.stats),ground(b.remote,b.stats);
 balloon.begin();ground.begin();LinkManager::Config c;c.autoProbe=false;
 CHECK(balloon.start(LinkManager::Role::Balloon,c,a.hostIO.now())==protocol::Result::Ok);
 CHECK(ground.start(LinkManager::Role::Ground,c,b.hostIO.now())==protocol::Result::Ok);
 ActivityCapture activity;a.remote.setActivityReceiver(&activity);
 MessageManager::Storage txStorage,rxStorage;
 MessageManager tx(a.remote,a.stats,txStorage),rx(b.remote,b.stats,rxStorage);tx.begin(123);rx.begin();
 struct Sink : messaging::Receiver { void onMessage(const messaging::MessageView&) override {} } sink;
 rx.setReceiver(&sink);
 messaging::SensorData data;
 data.internalTemperature=data.internalHumidity=data.internalLowTemperature=true;
 data.externalRtd=data.pressure=data.gps=true;data.pressurePa=82573;
 uint8_t encoded[64];CHECK(messaging::encodeSensors(data,encoded,sizeof(encoded))==44);
 bool started=true;unsigned restarts=0;uint32_t faultAt=5000,session=11,retryAt=0;
 const auto until=w.us+180000000;
 while(w.us<until) {
  if(injectFaults && a.hostIO.now()>=faultAt) {
   // Lose one command on the UART every five seconds. This models a fault,
   // not its physical cause; the code under investigation handles recovery.
   a.hostIO.corruptNext=4;faultAt=a.hostIO.now()+5000;
  }
  a.remote.poll();b.remote.poll();
  if(!a.remote.connected()) {
   started=false;
   if(int32_t(a.hostIO.now()-retryAt)>=0) {
    retryAt=a.hostIO.now()+2000;
    if(a.remote.connect(++session) && a.remote.initialize(radio::Config{})!=radio::Status::Ok)
     a.remote.disconnect();
   }
  }
  if(a.remote.connected() && !started) {
   started=balloon.start(LinkManager::Role::Balloon,c,a.hostIO.now())==protocol::Result::Ok;
   if(started)++restarts;
  }
  tx.poll(a.hostIO.now());rx.poll(b.hostIO.now());
  if(started)balloon.poll(a.hostIO.now());
  ground.poll(b.hostIO.now());
  if(started && balloon.phase()==LinkManager::Phase::Data && tx.ready(a.hostIO.now())) {
   tx.setInterPacketPause(2000);data.uptimeMs=a.hostIO.now();
   CHECK(tx.sendSensors(data,a.hostIO.now())==protocol::Result::Ok);
  }
  w.advance(1);
 }
 unsigned beacons=0,packets=0;
 for(const auto& p:activity.packets) {
  if(p.transmitting && p.type==protocol::PacketType::Beacon)++beacons;
  if(p.transmitting && p.type==protocol::PacketType::Data)++packets;
 }
 printf("faults=%u simulated_ms=%llu timeouts=%u link_restarts=%u data_tx=%u beacon_tx=%u ground_established=%u received_messages=%u\n",
  injectFaults,w.us/1000,a.remote.timeouts,restarts,packets,beacons,ground.established(),b.stats.snapshot().completedMessages);
 if(injectFaults) { CHECK(a.remote.timeouts>20);CHECK(restarts>20);CHECK(beacons==0);CHECK(!ground.established()); }
 else { CHECK(a.remote.timeouts==0);CHECK(beacons>0);CHECK(ground.established());CHECK(b.stats.snapshot().completedMessages>0); }
}
int main() { observe(false);observe(true); }
