#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <RemoteProtocol.h>

namespace status_led {
// RP2040 Zero WS2812, GP16. Packet notifications only update state;
// LED writes stay in poll(), outside the radio activity callback.
static Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
static uint32_t startedAt=0, displayed=0xffffffff;
static uint32_t activityAt=0, activityMs=0, activityColor=0;
static bool activityBlinks=false;
inline uint32_t packetColor(const board_link::PacketActivity& packet) {
 if(packet.type==protocol::PacketType::Beacon) return pixel.Color(48,16,0);
 if(packet.type==protocol::PacketType::Control) return pixel.Color(0,0,48);
 switch(packet.profile) {
 case 0: return pixel.Color(48,0,0);
 case 1: return pixel.Color(48,8,0);
 case 2: return pixel.Color(48,20,0);
 case 3: return pixel.Color(28,48,0);
 case 4: return pixel.Color(0,48,0);
 default: return 0;
 }
}
inline void onPacketActivity(const board_link::PacketActivity& packet) {
 activityAt=millis();
 // TX is known when accepted; RX type/profile are known only after decoding.
 // Give received and short packets a visible flash, without delaying the loop.
 activityMs=packet.transmitting && packet.durationMs>175 ? packet.durationMs : 175;
 activityColor=packetColor(packet);
 activityBlinks=packet.type==protocol::PacketType::Beacon || packet.type==protocol::PacketType::Control;
}
inline void display(uint32_t color) {
 if(color==displayed) return;
 pixel.setPixelColor(0,color);
 pixel.show();
 displayed=color;
}
inline void begin() {
 pixel.begin();
 startedAt=millis();
 activityMs=0;
 display(pixel.Color(24,24,24));
}
inline void poll(uint32_t now) {
 const uint32_t elapsed=now-startedAt;
 if(elapsed<2000) display(elapsed%400<200 ? pixel.Color(24,24,24):0);
 else {
  const uint32_t age=now-activityAt;
  display(age<activityMs && (!activityBlinks || age%175<100) ? activityColor:0);
 }
}
}
