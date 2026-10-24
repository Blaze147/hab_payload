#pragma once
#include <Arduino.h>
#include <BoardWire.h>
class BoardSerial : public board_link::IO {
public:
 void begin() {
#ifdef ARDUINO_ARCH_RP2040
  Serial1.setTX(0);Serial1.setRX(1);Serial1.setFIFOSize(1024);
#endif
  Serial1.begin(board_link::Baud);
 }
 int readByte() override { return Serial1.read(); }
 bool writeByte(uint8_t x) override {
  if(!Serial1.availableForWrite()) return false;
  const bool written=Serial1.write(x)==1;
#if defined(ARDUINO_ARCH_RP2040) && defined(BOARD_UART_TX_GAP_US)
  // Bench isolation: preserve baud/framing, but give the coprocessor time
  // between bytes to distinguish burst handling from RF payload limits.
  if(written)busy_wait_us_32(BOARD_UART_TX_GAP_US);
#endif
  return written;
 }
 uint32_t now() const override { return millis(); }
 void progress() override { yield(); }
};
