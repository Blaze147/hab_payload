static void restartPacketTests() {
 struct Capture : protocol::DataReceiver,protocol::LinkReceiver {
  unsigned count=0;bool restarted=false;
  protocol::Result onFragment(const protocol::Fragment& p) override {
   ++count;restarted=protocol::restartFlag(p.header.flags);return protocol::Result::Ok;
  }
  protocol::Result onControl(const protocol::ControlPacket& p,const radio::PacketInfo&) override {
   ++count;restarted=p.flags==protocol::RestartFlag;return protocol::Result::Ok;
  }
  protocol::Result onBeacon(const protocol::BeaconPacket&,const radio::PacketInfo&) override {
   ++count;restarted=false;return protocol::Result::Ok;
  }
 } capture;
 // Data or otherwise-unflagged control can consume the pending indication.
 for(unsigned family : {0u,2u}) {
  Pair p;StatisticsManager stats;ProtocolManager tx(p.balloon,stats),rx(p.ground,stats);
  tx.begin();rx.begin();rx.setDataReceiver(&capture);rx.setLinkReceiver(&capture);
  protocol::DataPacket data;data.header.payloadLength=1;data.payload[0]=42;
  protocol::BeaconPacket beacon;protocol::ControlPacket control;
  const auto send=[&]() {
   return family==0 ? tx.sendData(data,p.channel.now) : family==1 ?
    tx.sendBeacon(beacon,p.channel.now) : tx.sendControl(control,p.channel.now);
  };
  const auto decode=[&](bool expected) {
   const size_t offset=family==0 ? protocol::FlagsOffset : protocol::ControlFlagsOffset;
   CHECK(protocol::restartFlag(p.a.sending[offset])==expected);
   CHECK(p.a.sending[1]==family);
   radio::PacketInfo info;info.length=p.a.sending.size();
   const unsigned before=capture.count;
   CHECK(rx.receiveRaw(p.a.sending.data(),info.length,info)==protocol::Result::Ok);
   CHECK(capture.count==before+1 && capture.restarted==expected);
   // Flipping only the restart bit without recomputing CRC is rejected.
   auto corrupt=p.a.sending;corrupt[offset]^=protocol::RestartFlag;
   CHECK(rx.receiveRaw(corrupt.data(),info.length,info)==protocol::Result::InvalidArgument);
   CHECK(capture.count==before+1);
  };
  if(family==0) {
   tx.allowDataTransmission(false);CHECK(send()==protocol::Result::Busy);
   tx.allowDataTransmission(true);
   data.header.payloadLength=0;CHECK(send()==protocol::Result::InvalidArgument);data.header.payloadLength=1;
  }
  // Beacon bytes and SYN/SYN-ACK/ACK flags stay unchanged and leave pending set.
  CHECK(tx.sendBeacon(beacon,p.channel.now)==protocol::Result::Ok);
  CHECK(p.a.sending.size()==protocol::BeaconPacketSize && p.a.sending[1]==1);
  CHECK(p.a.sending[2]==0 && p.a.sending[3]==0 && p.a.sending[4]==10);
  p.finish(p.balloon);
  for(uint8_t flag : {uint8_t(1),uint8_t(3),uint8_t(2),uint8_t(4)}) {
   protocol::ControlPacket handshake;handshake.flags=flag;
   CHECK(tx.sendControl(handshake,p.channel.now)==protocol::Result::Ok);
   CHECK(p.a.sending[1]==2 && p.a.sending[3]==flag);
   CHECK(!tx.lastTransmissionRestarted());p.finish(p.balloon);
  }
  CHECK(send()==protocol::Result::Ok);decode(true);
  CHECK(send()==protocol::Result::Busy); // Busy cannot consume or create a flag.
  p.finish(p.balloon);CHECK(tx.poll(p.channel.now)==Status::Ok);
  CHECK(send()==protocol::Result::Ok);decode(false);p.finish(p.balloon);
  // A copied RX header, or a message/session rollover, is not a new reboot.
  data.header.flags=control.flags=protocol::RestartFlag;
  data.header.transmitterSessionId++;data.header.messageId=0;
  CHECK(send()==protocol::Result::Ok);decode(false);p.finish(p.balloon);
  // An explicit host indication survives failed starts and TX timeout.
  tx.markRestart();p.a.failTx=true;CHECK(send()==protocol::Result::HardwareError);
  p.a.failTx=false;CHECK(p.balloon.initialize(p.c)==Status::Ok);
  CHECK(send()==protocol::Result::Ok);decode(true);
  p.a.stuck=true;p.channel.advance(1000000);
  CHECK(tx.poll(p.channel.now)==Status::Timeout);
  p.a.stuck=false;CHECK(p.balloon.initialize(p.c)==Status::Ok);
  CHECK(send()==protocol::Result::Ok);decode(true);p.finish(p.balloon);
  CHECK(tx.poll(p.channel.now)==Status::Ok);
  // A new restart during a flagged TX must survive completion of that TX.
  tx.markRestart();CHECK(send()==protocol::Result::Ok);tx.markRestart();p.finish(p.balloon);
  CHECK(tx.poll(p.channel.now)==Status::Ok);
  CHECK(send()==protocol::Result::Ok);decode(true);p.finish(p.balloon);
  CHECK(send()==protocol::Result::Ok);decode(false);p.finish(p.balloon);
 }
 printf("PASS: restart in existing data/empty-control flags only; beacons and SYN/ACK unchanged; CRC, one-shot, failure retention, no RX echo\n");
}
