#pragma once
#include <RadioManager.h>
#include <LoRa.h>
#include <SPI.h>
class PhysicalRadioAdapter : public radio::Adapter {
 radio::PacketInfo pending_;
 // LoRa 0.8.0 has no public TX polling API. Same SPI settings and CS as driver.
 uint8_t reg(uint8_t address, uint8_t value=0, bool write=false) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(8, LOW); SPI.transfer(address | (write ? 0x80 : 0));
  uint8_t result=SPI.transfer(value); digitalWrite(8,HIGH); SPI.endTransaction(); return result;
 }
public:
 bool begin(const radio::Config& c) override {
  pending_.length=0; LoRa.setPins(8,4,7);
  return LoRa.begin(c.frequency) && configure(c);
 }
 bool configure(const radio::Config& c) override {
  pending_.length=0;
  if(reg(0x42)!=0x12) return false;
  LoRa.setFrequency(c.frequency); LoRa.setSpreadingFactor(c.spreadingFactor);
  LoRa.setSignalBandwidth(c.bandwidth); LoRa.setCodingRate4(c.codingRate);
  LoRa.setTxPower(c.power); LoRa.setPreambleLength(c.preamble); LoRa.setSyncWord(c.syncWord);
  LoRa.disableInvertIQ(); if(c.crc) LoRa.enableCrc(); else LoRa.disableCrc(); return true;
 }
 bool idle() override { LoRa.idle(); pending_.length=0; return true; }
 bool receive() override { if(!pending_.length) LoRa.receive(); return true; }
 bool transmit(const uint8_t* p,size_t n) override {
  pending_.length=0; LoRa.idle();
  reg(0x12,0x08,true); // Clear stale TX_DONE before a new transmission.
  return LoRa.beginPacket() && LoRa.write(p,n)==n && LoRa.endPacket(true);
 }
 radio::TxStatus pollTx() override {
  if(reg(0x42)!=0x12) return radio::TxStatus::Failed;
  if(reg(0x12)&0x08) { reg(0x12,0x08,true); return radio::TxStatus::Complete; }
  return radio::TxStatus::InProgress;
 }
 radio::Status read(uint8_t* p,size_t capacity,radio::PacketInfo& info) override {
  if(reg(0x42)!=0x12) return radio::Status::HardwareError;
  if(!pending_.length) {
   if(!(reg(0x12)&0x40)) return radio::Status::NoPacket;
   int n=LoRa.parsePacket();
   if(n<=0) { LoRa.receive(); return radio::Status::InvalidPayload; }
   pending_.length=n; pending_.rssi=LoRa.packetRssi(); pending_.snr=LoRa.packetSnr();
  }
  info=pending_; if(capacity<info.length) return radio::Status::BufferTooSmall;
  for(size_t i=0;i<info.length;++i) p[i]=uint8_t(LoRa.read());
  pending_.length=0; LoRa.receive(); return radio::Status::Ok;
 }
};
