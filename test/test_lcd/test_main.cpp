#include "GroundLcdView.h"
#include <assert.h>
#include <string>
#include <iostream>
using namespace ground_lcd;
int main() {
 Telemetry sample;char output[17];formatTop(sample,output);
 assert(std::string(output)=="P? R -- S --    ");
 sample.profile=0;sample.signal=true;sample.rssi=-52;sample.snr=11.75f;
 formatTop(sample,output);assert(std::string(output)=="P0 R -52 S 11.8 ");
 sample.profile=4;sample.rssi=-123;sample.snr=-12.25f;
 formatTop(sample,output);assert(std::string(output)=="P4 R-123 S-12.3 ");
 sample.snr=11.75f;formatTop(sample,output);assert(std::string(output)=="P4 R -123 S 11.8");
 sample.rssi=-999;sample.snr=-99;formatTop(sample,output);assert(std::string(output)=="P4 R-999 S-99.0 ");
 char value[20];coordinate(-105016780,value,sizeof(value));assert(std::string(value)=="-105.017");
 coordinate(39999500,value,sizeof(value));assert(std::string(value)=="40.000");
 CoordinateScroll scroll;sample.position=true;sample.latE6=39563668;sample.lonE6=-105016780;sample.positionAt=1000;
 const std::string frozen="39.564,-105.017 -5S    ";
 for(size_t i=0;i<frozen.size();++i) {
  scroll.frame(sample,6000+uint32_t(i)*1000,output);
  std::string expected;
  for(size_t j=0;j<16;++j) expected+=frozen[(i+j)%frozen.size()];
  assert(std::string(output)==expected);
  // New coordinates and age must wait until the next complete cycle.
  sample.latE6=40000000;sample.lonE6=-106000000;sample.positionAt=6000;
 }
 scroll.frame(sample,126000,output);assert(std::string(output)=="40.000,-106.000 ");
 const std::string next="40.000,-106.000 -120S    ";
 for(size_t i=1;i<next.size();++i) {
  scroll.frame(sample,130000+uint32_t(i)*1000,output);
  std::string expected;for(size_t j=0;j<16;++j) expected+=next[(i+j)%next.size()];
  assert(std::string(output)==expected);
 }
 std::cout<<"PASS: fixed 16-character signal rows, rounding, frozen coordinates and age, next-cycle refresh\n";
}
