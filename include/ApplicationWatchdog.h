#pragma once
#ifdef APPLICATION_WATCHDOG
#include <hardware/watchdog.h>
#include <hardware/structs/usb.h>
#include <hardware/structs/nvic.h>
#include <hardware/structs/timer.h>
#ifndef USE_TINYUSB
#include <USB.h>
#endif
namespace application_watchdog {
constexpr uint32_t Armed=0x55534250;
static const bool recovering=watchdog_enable_caused_reboot() && watchdog_hw->scratch[0]==Armed;
static volatile uint32_t trace[32] __attribute__((section(".uninitialized_data")));
inline void stage(uint32_t value) {
 const uint32_t core=get_core_num();
 trace[5+core]=value;trace[9+core]=millis();
 uint32_t mask;asm volatile("mrs %0, primask" : "=r"(mask));trace[11+core]=mask;
 if(!core) {
  trace[13]=usb_hw->sof_rd;trace[14]=usb_hw->ints;
  if(trace[13]!=trace[30]) {trace[30]=trace[13];trace[31]=millis();}
  trace[16]=nvic_hw->iser;trace[17]=nvic_hw->ispr;
  trace[18]=timer_hw->inte;trace[19]=timer_hw->intr;
  trace[20]=timer_hw->armed;trace[21]=timer_hw->alarm[3];
  trace[22]=uint32_t(digitalRead(2)) | (uint32_t(digitalRead(3))<<1);
  trace[24]=usb_hw->main_ctrl;trace[25]=usb_hw->sie_status;
  trace[26]=usb_hw->sie_ctrl;trace[27]=usb_hw->inte;
  trace[28]=usb_hw->buf_status;trace[29]=usb_hw->dev_addr_ctrl;
#ifndef USE_TINYUSB
  trace[23]=USB.mutex.owner;
#endif
 }
}
// Only a completed main-loop iteration feeds the hardware watchdog. There is
// no PC heartbeat, elapsed-runtime deadline, or diagnostic crash command.
inline void progress() {watchdog_update();}
inline void begin() {
 trace[0]=Armed;trace[1]=watchdog_hw->reason;trace[2]=watchdog_hw->scratch[4];trace[3]=watchdog_hw->scratch[0];
 // A timeout always returns to the application, including sensor core 1.
 // BOOTSEL is an explicit USB maintenance command, never a reset policy.
 watchdog_hw->scratch[0]=0;
 trace[4]=1;
 for(unsigned i=5;i<32;++i) trace[i]=0;
 trace[8]=recovering ? 1 : 0;
 watchdog_hw->scratch[0]=Armed;
 watchdog_enable(8000,true);
}
}
#endif
