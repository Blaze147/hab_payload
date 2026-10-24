#include <Arduino.h>
#include <SPI.h>
#include <SDFS.h>
void setup() {
 Serial.begin(115200);
 pinMode(9,OUTPUT);digitalWrite(9,HIGH);
 pinMode(13,OUTPUT);digitalWrite(13,HIGH);
 SPI.setRX(4);SPI.setTX(7);SPI.setSCK(6);
 pinMode(5,OUTPUT);digitalWrite(5,HIGH);
}
void loop() {
 if(!Serial) {delay(100);return;}
 SdFat sd;
 bool card=sd.cardBegin(SdSpiConfig(5,SHARED_SPI,250000,&SPI));
 Serial.printf("BALLOON_SD MISO=4 MOSI=7 SCK=6 CS=5 hz=250000 card=%u error=0x%02x data=0x%02x\r\n",card,sd.sdErrorCode(),sd.sdErrorData());
 if(card) {
 Serial.printf("BALLOON_SD sectors=%lu size_MB=%.1f type=%u\r\n",sd.card()->sectorCount(),sd.card()->sectorCount()*512.0/1000000.0,sd.card()->type());
 bool volume=sd.volumeBegin();Serial.printf("BALLOON_SD filesystem=%u\r\n",volume);
 if(volume) {Serial.println("BALLOON_SD root listing:"); sd.ls(&Serial,LS_SIZE);}
 }
 sd.end();delay(5000);
}
