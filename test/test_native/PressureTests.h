#pragma once
#include "sensors/Bmp280Compensation.h"
#include <cmath>
#include <vector>
void pressureTelemetryTests() {
 // Bosch data-sheet reference coefficients/raw ADC inputs, no live sensor needed.
 const int32_t coefficients[]={27504,26435,-1000,36477,-10685,3024,2855,140,-7,15500,-14600,6000};
 uint8_t bytes[24];
 for(unsigned i=0;i<12;++i) {const uint16_t v=uint16_t(coefficients[i]);bytes[2*i]=uint8_t(v);bytes[2*i+1]=uint8_t(v>>8);}
 bmp280::Calibration c;CHECK(bmp280::calibration(bytes,sizeof(bytes),c));
 CHECK(c.t1==27504 && c.t3==-1000 && c.p2==-10685 && c.p9==6000);
 double temperature=0,pressure=0;
 CHECK(bmp280::compensate(c,519888,415148,temperature,pressure));
 CHECK(std::abs(temperature-25.08247793)<0.00001);
 CHECK(std::abs(pressure-100653.2668)<0.01);
 const uint8_t raw[]={0x65,0x5a,0xc0,0x7e,0xed,0x00};
 CHECK(bmp280::raw20(raw)==415148 && bmp280::raw20(raw+3)==519888);
 CHECK(!bmp280::compensate(c,0x80000,415148,temperature,pressure));
 CHECK(!bmp280::compensate(c,519888,0x80000,temperature,pressure));
 CHECK(!bmp280::compensate(c,0x100000,415148,temperature,pressure));
 CHECK(!bmp280::calibration(bytes,23,c));
 uint8_t blank[24]={};CHECK(!bmp280::calibration(blank,sizeof(blank),c));
 c.p1=0;CHECK(!bmp280::compensate(c,519888,415148,temperature,pressure));
 // All requested sensors, through the real encoder/protocol/radio/receiver path.
 Pair wire;StatisticsManager txStats,rxStats;txStats.begin();rxStats.begin();
 ProtocolManager tx(wire.balloon,txStats),rx(wire.ground,rxStats);tx.begin();rx.begin();
 MessageManager::Storage txStorage,rxStorage;
 MessageManager sender(tx,txStats,txStorage),receiver(rx,rxStats,rxStorage);
 sender.begin(0x1234);receiver.begin();
 struct Sink : messaging::Receiver {
  bool sensors=false,camera=false;std::vector<uint8_t> bytes;
  void onMessage(const messaging::MessageView& m) override {CHECK(m.length==44);bytes.assign(m.data,m.data+m.length);}
  void onSensors(const messaging::SensorData& s,const messaging::MessageView&) override {
   CHECK(s.internalTemperature && s.internalHumidity && s.internalLowTemperature && s.externalRtd && s.pressure && s.gps);
   CHECK(s.internalTemperatureCentiC==2750 && s.internalHumidityCentiPct==3050);
   CHECK(s.internalLowTemperatureCentiC==2200 && s.externalRtdCentiC==2300 && s.pressurePa==100653);
   CHECK(s.gpsFlags==15 && s.latitudeE6==39500000 && s.longitudeE6==-105000000 && s.altitudeDm==17000 && s.satellites==12);
   sensors=true;
  }
  void onCamera(const messaging::CameraView&,const messaging::MessageView&) override {camera=true;}
 } sink;
 receiver.setReceiver(&sink);
 auto config=wire.c;config.spreadingFactor=7;config.bandwidth=500000;config.codingRate=5;
 CHECK(wire.balloon.configure(config)==Status::Ok);CHECK(wire.ground.configure(config)==Status::Ok);CHECK(rx.receive()==Status::Ok);
 messaging::SensorData s;s.uptimeMs=123;
 s.internalTemperature=s.internalHumidity=s.internalLowTemperature=s.externalRtd=s.pressure=s.gps=true;
 s.internalTemperatureCentiC=2750;s.internalHumidityCentiPct=3050;s.internalLowTemperatureCentiC=2200;
 s.externalRtdCentiC=2300;s.pressurePa=100653;s.gpsFlags=15;s.latitudeE6=39500000;s.longitudeE6=-105000000;s.altitudeDm=17000;s.satellites=12;
 CHECK(sender.sendSensors(s,0)==protocol::Result::Ok);
 for(unsigned t=0;t<1000 && !sink.sensors;++t) {wire.channel.advance(1);tx.poll(wire.channel.now);rx.poll(wire.channel.now);sender.poll(wire.channel.now);receiver.poll(wire.channel.now);}
 CHECK(sink.sensors && !sink.camera);
 printf("ALL_SENSOR_GOLDEN ");for(auto b:sink.bytes)printf("%02x",b);printf("\n");
 printf("PASS: BMP280 reference compensation, invalid ADC/calibration rejection, six sensor records over packet/radio pipeline; no image\n");
}
