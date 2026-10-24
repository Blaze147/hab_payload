#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <hardware/gpio.h>
#include <hardware/watchdog.h>
#include <pico/time.h>
OneWire bus;
DallasTemperature sensors(&bus);
void setup() {
 Serial.begin(115200);bus.begin(26);sensors.setAutoSaveScratchPad(false);
 watchdog_enable(8000,true);
}
void loop() {
 watchdog_update();
 if(!Serial) { delay(10);return; }
 // Independent direct-GPIO reset and 5 us waveform sampling.
 pinMode(26,INPUT);
 const bool idle=digitalRead(26);
 uint8_t samples[80];
 noInterrupts();gpio_put(26,0);gpio_set_dir(26,true);busy_wait_us_32(480);
 gpio_set_dir(26,false);
 for(unsigned i=0;i<80;++i) { busy_wait_us_32(5);samples[i]=gpio_get(26); }
 interrupts();
 int low=0,first=-1,last=-1;
 for(unsigned i=0;i<80;++i)if(!samples[i]) {++low;if(first<0)first=int(i);last=int(i);}
 const bool presence=bus.reset();
 sensors.begin();
 Serial.print("DS_PROBE gp=26 idle_high=");Serial.print(idle);
 Serial.print(" library_presence=");Serial.print(presence);
 Serial.print(" devices=");Serial.print(sensors.getDeviceCount());
 Serial.print(" sampled_low_slots=");Serial.print(low);
 Serial.print(" first_low_slot=");Serial.print(first);
 Serial.print(" last_low_slot=");Serial.println(last);
 DeviceAddress address;
 for(unsigned i=0;i<sensors.getDeviceCount();++i)if(sensors.getAddress(address,i)) {
  Serial.print("ROM ");for(unsigned j=0;j<8;++j) {if(address[j]<16)Serial.print('0');Serial.print(address[j],HEX);}
  Serial.print(" c=");sensors.requestTemperaturesByAddress(address);Serial.println(sensors.getTempC(address),2);
 }
 delay(1000);
}
