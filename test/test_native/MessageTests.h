#pragma once
#include <vector>
void messageManagerTests() {
 using namespace messaging;
 uint8_t encoded[MaxSensorBytes];SensorData s;s.uptimeMs=123;
 s.internalTemperature=true;s.internalTemperatureCentiC=2570;
 s.internalHumidity=true;s.internalHumidityCentiPct=4060;
 s.externalRtd=true;s.externalRtdCentiC=-3089;
 const size_t length=encodeSensors(s,encoded,sizeof(encoded));
 const uint8_t golden[]={0xa1,0,0,0,123,1,2,0x0a,0x0a,2,2,0x0f,0xdc,4,2,0xf3,0xef};
 CHECK(length==sizeof(golden));CHECK(!memcmp(encoded,golden,length));
 SensorData decoded;CameraView camera;
 CHECK(decode(encoded,length,decoded,camera));CHECK(decoded.externalRtdCentiC==-3089);
 CHECK(!decoded.internalLowTemperature && !decoded.pressure && !decoded.gps);
 for(size_t n=1;n<5;++n)CHECK(!decode(encoded,n,decoded,camera));
 for(size_t n=6;n<length;++n)if(n!=9 && n!=13)CHECK(!decode(encoded,n,decoded,camera));
 uint8_t malformed[MaxSensorBytes];memcpy(malformed,encoded,length);
 malformed[9]=1;CHECK(!decode(malformed,length,decoded,camera)); // duplicate ID
 memcpy(malformed,encoded,length);malformed[5]=42;CHECK(!decode(malformed,length,decoded,camera));
 s.gps=true;s.gpsFlags=15;s.latitudeE6=0;s.longitudeE6=-180000000;s.altitudeDm=-500;s.satellites=20;
 s.internalLowTemperature=true;s.internalLowTemperatureCentiC=-4000;s.pressure=true;s.pressurePa=1000;
 const auto all=encodeSensors(s,encoded,sizeof(encoded));CHECK(all && all<=MaxSensorBytes);
 CHECK(decode(encoded,all,decoded,camera));CHECK(decoded.longitudeE6==-180000000 && decoded.altitudeDm==-500);
 for(int t:{-5500,-4000,-2000,0,12500}) {
  s.internalLowTemperatureCentiC=int16_t(t);
  const auto n=encodeSensors(s,encoded,sizeof(encoded));CHECK(n);
  CHECK(decode(encoded,n,decoded,camera));CHECK(decoded.internalLowTemperatureCentiC==t);
 }
 s.internalLowTemperatureCentiC=-5501;CHECK(!encodeSensors(s,encoded,sizeof(encoded)));
 s.internalLowTemperatureCentiC=12501;CHECK(!encodeSensors(s,encoded,sizeof(encoded)));
 s.internalLowTemperatureCentiC=-4000;
 s.latitudeE6=90000001;CHECK(!encodeSensors(s,encoded,sizeof(encoded)));s.latitudeE6=0;
 s.internalHumidityCentiPct=10001;CHECK(!encodeSensors(s,encoded,sizeof(encoded)));
 // Reverse-order assembly, lost middle fragment, duplicates, conflict and timeout.
 Pair p;StatisticsManager stats;stats.begin();ProtocolManager protocol(p.balloon,stats);protocol.begin();
 MessageManager::Storage storage;MessageManager manager(protocol,stats,storage);manager.begin(99);
 struct Capture : Receiver {
  unsigned count=0,sensors=0,cameras=0;std::vector<uint8_t> bytes;
  void onMessage(const MessageView& m) override { ++count;bytes.assign(m.data,m.data+m.length); }
  void onSensors(const SensorData& d,const MessageView&) override { CHECK(d.externalRtdCentiC==-3089);++sensors; }
  void onCamera(const CameraView& c,const MessageView&) override { CHECK(c.length==32768 && c.width==320);++cameras; }
 } cap;
 manager.setReceiver(&cap);
 std::vector<uint8_t> bytes(protocol::MaxMessageSize,0x55);
 const auto count=protocol::MaxFragmentCount;
 auto fragment=[&](unsigned id,unsigned index,uint32_t now)->protocol::Fragment {
  protocol::Fragment f;f.header.transmitterSessionId=12;f.header.messageId=uint16_t(id);
  f.header.fragmentCount=count;f.header.fragmentIndex=uint16_t(index);
  const size_t at=index*protocol::MaxPayloadSize;
  f.header.payloadLength=uint8_t(std::min(bytes.size()-at,protocol::MaxPayloadSize));
  f.reception.receivedAtMs=now;memcpy(f.rawBytes+protocol::PayloadOffset,bytes.data()+at,f.header.payloadLength);
  return f;
 };
 for(unsigned i=count;i-->1;) {auto f=fragment(1,i,10);CHECK(manager.onFragment(f)==protocol::Result::Ok);}
 CHECK(cap.count==0);auto f=fragment(1,2,11);CHECK(manager.onFragment(f)==protocol::Result::Ok);
 f=fragment(2,0,12);CHECK(manager.onFragment(f)==protocol::Result::Busy);
 f=fragment(1,0,13);CHECK(manager.onFragment(f)==protocol::Result::Ok);
 CHECK(cap.count==1 && cap.bytes==bytes);CHECK(manager.onFragment(f)==protocol::Result::Ok);CHECK(cap.count==1);
 f=fragment(2,0,14);CHECK(manager.onFragment(f)==protocol::Result::Ok);
 f.rawBytes[protocol::PayloadOffset]^=1;CHECK(manager.onFragment(f)==protocol::Result::InvalidArgument);
 f=fragment(3,0,UINT32_MAX-100);CHECK(manager.onFragment(f)==protocol::Result::Ok);
 manager.poll(MessageManager::ReassemblyGapMs-100);
 f=fragment(4,1,MessageManager::ReassemblyGapMs);CHECK(manager.onFragment(f)==protocol::Result::Ok);
 CHECK(cap.count==1);manager.poll(2*MessageManager::ReassemblyGapMs);
 // Typed malformed data is never delivered as an application message.
 protocol::Fragment bad;bad.header.payloadLength=2;bad.rawBytes[protocol::PayloadOffset]=CodecVersion;
 CHECK(manager.onFragment(bad)==protocol::Result::InvalidArgument);CHECK(cap.count==1);
 // A complete 32 KiB image fits one typed message and automatically fragments.
 std::vector<uint8_t> jpeg(32768,0x33);jpeg[0]=0xff;jpeg[1]=0xd8;jpeg[32766]=0xff;jpeg[32767]=0xd9;
 CameraView image;image.width=320;image.height=240;image.jpeg=jpeg.data();image.length=jpeg.size();
 std::vector<uint8_t> imageBytes(protocol::MaxMessageSize);
 const auto imageLength=encodeCamera(image,42,imageBytes.data(),imageBytes.size());
 CHECK(imageLength==32781);CHECK(decode(imageBytes.data(),imageLength,decoded,camera));
 CHECK(camera.length==jpeg.size());
 // Real packet encoding/CRC/radio simulation, paced sender -> receiver manager.
 Pair wire;StatisticsManager txStats,rxStats;txStats.begin();rxStats.begin();
 ProtocolManager tx(wire.balloon,txStats),rx(wire.ground,rxStats);tx.begin();rx.begin();
 MessageManager::Storage txStorage,rxStorage;
 MessageManager sender(tx,txStats,txStorage),receiver(rx,rxStats,rxStorage);
 sender.begin(77);receiver.begin();receiver.setReceiver(&cap);
 auto config=wire.c;config.spreadingFactor=7;config.bandwidth=500000;config.codingRate=5;
 CHECK(wire.balloon.configure(config)==Status::Ok);CHECK(wire.ground.configure(config)==Status::Ok);CHECK(rx.receive()==Status::Ok);
 sender.setInterPacketPause(10);
 CHECK(sender.sendCamera(image,wire.channel.now)==protocol::Result::Ok);
 CHECK(sender.sendCamera(image,wire.channel.now)==protocol::Result::Busy);
 for(unsigned t=0;t<30000 && (sender.busy() || cap.cameras==0);++t) {
  wire.channel.advance(1);CHECK(tx.poll(wire.channel.now)==Status::Ok);CHECK(rx.poll(wire.channel.now)==Status::Ok);
  sender.poll(wire.channel.now);receiver.poll(wire.channel.now);
 }
 CHECK(!sender.busy());CHECK(cap.cameras==1);CHECK(cap.bytes.size()==imageLength);
 CHECK(!memcmp(cap.bytes.data(),imageBytes.data(),1)); // version (uptime differs)
 CHECK(!memcmp(cap.bytes.data()+5,imageBytes.data()+5,imageLength-5));
 // Sensor callback is decoded by manager; app receives typed values.
 MessageView m;m.data=golden;m.length=sizeof(golden);
 while(!sender.ready(wire.channel.now)) {wire.channel.advance(1);tx.poll(wire.channel.now);rx.poll(wire.channel.now);}
 CHECK(sender.send(m,wire.channel.now)==protocol::Result::Ok);
 for(unsigned t=0;t<1000 && !cap.sensors;++t) {wire.channel.advance(1);tx.poll(wire.channel.now);rx.poll(wire.channel.now);receiver.poll(wire.channel.now);}
 CHECK(cap.sensors==1);
 printf("PASS: typed codec, cold temperatures, malformed records, reverse assembly, loss/duplicate/conflict/timeout, 32 KiB camera over packet pipeline\n");
}
