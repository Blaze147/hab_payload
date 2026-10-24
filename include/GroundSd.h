#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SDFS.h>
#include <atomic>

// Ground-only SPI1 peripheral. All filesystem work stays on core 1 so card
// latency cannot block the shared radio UART service on core 0.
namespace ground_sd {
enum Result : uint8_t { Pending, Passed, PinsFailed, MountFailed, NameFailed,
 WriteFailed, ReadFailed, VerifyFailed, CleanupFailed };
static std::atomic<uint8_t> result{Pending};
static char testPath[24];
static bool diagnosticCard=false,diagnosticVolume=false;
static uint8_t diagnosticCode=0,diagnosticData=0;
inline uint8_t pattern(size_t offset) { return uint8_t((offset * 37u + 19u) & 255u); }
inline void begin() {
 if(!SPI1.setSCK(10) || !SPI1.setTX(11) || !SPI1.setRX(12)) {
  result.store(PinsFailed);return;
 }
 pinMode(9,OUTPUT);digitalWrite(9,HIGH);
 // Conservative 1 MHz clock; never format a user's card on mount failure.
 SDFSConfig config(9,1000000,SPI1);
 config.setAutoFormat(false);
 if(!SDFS.setConfig(config) || !SDFS.begin()) {
  SDFS.end();
  SdFat probe;
  diagnosticCard=probe.cardBegin(SdSpiConfig(9,SHARED_SPI,250000,&SPI1));
  if(diagnosticCard) diagnosticVolume=probe.volumeBegin();
  diagnosticCode=probe.sdErrorCode();diagnosticData=probe.sdErrorData();
  probe.end();
  result.store(MountFailed);return;
 }
 bool found=false;
 for(unsigned i=0;i<1000;++i) {
  snprintf(testPath,sizeof(testPath),"/GS%06u.TST",i);
  if(!SDFS.exists(testPath)) { found=true;break; }
 }
 if(!found) { result.store(NameFailed);return; }
 uint8_t block[512];
 File file=SDFS.open(testPath,"w");
 if(!file) { result.store(WriteFailed);return; }
 for(size_t offset=0;offset<1024;offset+=sizeof(block)) {
  for(size_t j=0;j<sizeof(block);++j) block[j]=pattern(offset+j);
  if(file.write(block,sizeof(block))!=sizeof(block)) {
   file.close();result.store(WriteFailed);return;
  }
 }
 file.flush();file.close();
 file=SDFS.open(testPath,"r");
 if(!file) { result.store(ReadFailed);return; }
 bool valid=file.size()==1024;
 for(size_t offset=0;valid && offset<1024;offset+=sizeof(block)) {
  valid=file.read(block,sizeof(block))==int(sizeof(block));
  for(size_t j=0;valid && j<sizeof(block);++j) valid=block[j]==pattern(offset+j);
 }
 file.close();
 if(!valid) { result.store(VerifyFailed);return; }
 if(!SDFS.remove(testPath) || SDFS.exists(testPath)) { result.store(CleanupFailed);return; }
 result.store(Passed);
}
inline void report() {
 static bool reported=false;
 static uint32_t reportedAt=0;
 const auto value=result.load();
 if(!Serial || value==Pending || (reported && uint32_t(millis()-reportedAt)<10000)) return;
 reported=true;reportedAt=millis();
 Serial.println("GROUND_SD SPI1 CS=GP9 CLK=GP10 MOSI=GP11 MISO=GP12 hz=1000000");
 if(value==Passed) Serial.println("GROUND_SD PASS written=1024 read=1024 verified=1024 removed=1");
 else {
  const char* names[]={"pending","passed","pins","mount","unused_filename","write","read","verify","cleanup"};
  Serial.print("GROUND_SD FAIL stage=");Serial.println(names[value]);
  if(value==MountFailed) {
   Serial.print("GROUND_SD diagnostic_hz=250000 card=");Serial.print(diagnosticCard);
   Serial.print(" filesystem=");Serial.print(diagnosticVolume);
   Serial.print(" error=0x");Serial.print(diagnosticCode,HEX);
   Serial.print(" data=0x");Serial.println(diagnosticData,HEX);
  }
  if(testPath[0]) { Serial.print("GROUND_SD test_path=");Serial.println(testPath); }
 }
}
}
