#pragma once
// Temporary opt-in bus comparison, under the full balloon workload. Both
// backends use the same ROM/conversion/scratchpad protocol, without EEPROM writes.
#include <hardware/gpio.h>
#include <hardware/sync.h>
namespace internal_temperature_monitor { namespace diagnostic {
struct Trace {
 uint32_t sequence=0, at=0;
 uint8_t mode=0, rom[8]={}, scratch[9]={};
 bool presence=false, romCrc=false, scratchPresence=false, scratchCrc=false;
 const char* stage="STARTING";
};
static Trace trace, published;
auto_init_mutex(traceMutex);
static uint32_t started=0, due=0, requested=0;
static bool converting=false;
inline void release() { gpio_set_dir(DataPin,false); }
inline void low() { gpio_put(DataPin,false);gpio_set_dir(DataPin,true); }
inline bool reset() {
 if(!trace.mode)return bus.reset()!=0;
 release();busy_wait_us_32(5);
 if(!gpio_get(DataPin))return false;
 low();busy_wait_us_32(480);
 const uint32_t irq=save_and_disable_interrupts();
 release();busy_wait_us_32(70);const bool present=!gpio_get(DataPin);
 restore_interrupts(irq);busy_wait_us_32(410);return present;
}
inline void write(uint8_t value) {
 if(!trace.mode) { bus.write(value);return; }
 for(unsigned i=0;i<8;++i) {
  const bool one=value&(1u<<i);
  const uint32_t irq=save_and_disable_interrupts();
  low();busy_wait_us_32(one?6:60);release();
  restore_interrupts(irq);busy_wait_us_32(one?64:10);
 }
}
inline uint8_t readByte() {
 if(!trace.mode)return bus.read();
 uint8_t value=0;
 for(unsigned i=0;i<8;++i) {
  const uint32_t irq=save_and_disable_interrupts();
  low();busy_wait_us_32(3);release();busy_wait_us_32(10);
  if(gpio_get(DataPin))value|=1u<<i;
  restore_interrupts(irq);busy_wait_us_32(53);
 }
 return value;
}
inline void select() { write(0x55);for(uint8_t b:trace.rom)write(b); }
inline void finish(const char* stage,float c=NAN) {
 trace.stage=stage;trace.at=millis();++trace.sequence;
 mutex_enter_blocking(&traceMutex);published=trace;mutex_exit(&traceMutex);
 found=trace.romCrc;deviceCount=trace.romCrc?1:0;
 idleHigh=gpio_get(DataPin);presence=trace.presence;
 if(found)memcpy(address,trace.rom,8);
 publish(c,stage,trace.at);converting=false;due=millis()+1000;
}
inline void begin() { started=millis();due=started; }
inline void read() {
 const uint32_t now=millis();
 if(converting) {
  if(uint32_t(now-requested)<800)return;
  trace.scratchPresence=reset();
  if(!trace.scratchPresence) {finish("DIAG_SCRATCH_NO_PRESENCE");return;}
  select();write(0xbe);
  for(auto& b:trace.scratch)b=readByte();
  release();
  bool nonzero=false;for(auto b:trace.scratch)nonzero|=b!=0;
  trace.scratchCrc=nonzero && OneWire::crc8(trace.scratch,8)==trace.scratch[8];
  if(!trace.scratchCrc) {finish("DIAG_SCRATCH_CRC");return;}
  const float c=int16_t(uint16_t(trace.scratch[0])|(uint16_t(trace.scratch[1])<<8))/16.0f;
  finish(c>=-55 && c<=125?"OK":"DIAG_RANGE",c>=-55 && c<=125?c:NAN);return;
 }
 if(int32_t(now-due)<0)return;
 // Alternate 30-second windows, repeated, to reduce order/warm-up bias.
 const uint8_t mode=((now-started)/30000)%2;
 const uint32_t seq=trace.sequence;trace=Trace{};trace.mode=mode;trace.sequence=seq;
 trace.presence=reset();
 if(!trace.presence) {finish("DIAG_ROM_NO_PRESENCE");return;}
 write(0x33); // Read ROM: comparison assumes the one installed probe on GP26.
 for(auto& b:trace.rom)b=readByte();
 release();
 trace.romCrc=trace.rom[0]==0x28 && OneWire::crc8(trace.rom,7)==trace.rom[7];
 if(!trace.romCrc) {finish("DIAG_ROM_CRC");return;}
 if(!reset()) {finish("DIAG_CONVERT_NO_PRESENCE");return;}
 select();write(0x44);release();requested=millis();converting=true;
}
inline void poll() {
 static uint32_t reported=0;Trace snapshot;
 if(!mutex_try_enter(&traceMutex,nullptr))return;
 snapshot=published;mutex_exit(&traceMutex);
 if(!snapshot.sequence || snapshot.sequence==reported || !Serial)return;
 char rom[17],scratch[19],line[256];
 for(unsigned i=0;i<8;++i)snprintf(rom+2*i,3,"%02X",snapshot.rom[i]);
 for(unsigned i=0;i<9;++i)snprintf(scratch+2*i,3,"%02X",snapshot.scratch[i]);
 const int n=snprintf(line,sizeof(line),
  "DS_BUS backend=%s stage=%s rom=%s rom_crc=%u scratch=%s scratch_presence=%u scratch_crc=%u presence=%u ms=%lu sequence=%lu\r\n",
  snapshot.mode?"gpio":"library",snapshot.stage,rom,snapshot.romCrc,scratch,
  snapshot.scratchPresence,snapshot.scratchCrc,snapshot.presence,
  (unsigned long)snapshot.at,(unsigned long)snapshot.sequence);
 if(n>0 && size_t(n)<sizeof(line) && usb_output::availableForWrite()>=n) {
  Serial.write(reinterpret_cast<const uint8_t*>(line),size_t(n));reported=snapshot.sequence;
 }
}
} }
