#include <Arduino.h>
#include <Wire.h>
#include <TinyGPS++.h>
TinyGPSPlus oldGps,newGps;
uint32_t lastReport=0,lastPoll=0,errors=0,bytesRead=0;
bool ack=false;
void setup() {
 Serial.begin(115200);
 Serial2.setTX(4);Serial2.setRX(5);Serial2.setFIFOSize(2048);Serial2.begin(9600);
 Wire1.setSDA(14);Wire1.setSCL(15);Wire1.begin();Wire1.setClock(100000);Wire1.setTimeout(20);
}
void report(const char* name,TinyGPSPlus& gps) {
 Serial.print(name);Serial.print(" fix=");Serial.print(gps.location.isValid() && gps.location.age()<3000);
 Serial.print(" lat=");Serial.print(gps.location.lat(),6);Serial.print(" lon=");Serial.print(gps.location.lng(),6);
 Serial.print(" alt_m=");Serial.print(gps.altitude.meters(),1);Serial.print(" speed_kmh=");Serial.print(gps.speed.kmph(),2);
 Serial.print(" sats=");Serial.print(gps.satellites.value());Serial.print(" hdop=");Serial.print(gps.hdop.hdop(),2);
 Serial.printf(" utc=%04u-%02u-%02uT%02u:%02u:%02uZ chars=%lu checksum_fail=%lu age_ms=%lu\r\n",gps.date.year(),gps.date.month(),gps.date.day(),gps.time.hour(),gps.time.minute(),gps.time.second(),gps.charsProcessed(),gps.failedChecksum(),gps.location.age());
}
void loop() {
 for(unsigned n=0;n<512 && Serial2.available();++n) oldGps.encode(Serial2.read());
 uint32_t now=millis();
 if(now-lastPoll>=50) {
  lastPoll=now;
  Wire1.beginTransmission(0x42);Wire1.write(0xfd);
  ack=Wire1.endTransmission(false)==0;
  if(ack && Wire1.requestFrom(uint8_t(0x42),uint8_t(2))==2) {
   unsigned available=unsigned(Wire1.read())<<8;available|=Wire1.read();
   if(available==65535) available=0;
   if(available>512) available=512;
   // The register pointer advances from 0xFD/0xFE to the 0xFF stream register.
   while(available) {
    unsigned chunk=available>32?32:available;
    unsigned got=Wire1.requestFrom(uint8_t(0x42),uint8_t(chunk));
    if(!got) {++errors;break;}
    while(Wire1.available()) {newGps.encode(Wire1.read());++bytesRead;}
    available-=got;
   }
  } else ++errors;
 }
 if(Serial && now-lastReport>=1000) {
  lastReport=now;
  Serial.printf("I2C address=0x42 sda=14 scl=15 ack=%u bytes=%lu errors=%lu\r\n",ack,bytesRead,errors);
  report("GTU7",oldGps);report("MAXM10S",newGps);
 }
}
