#pragma once

#include "GroundSd.h"
#include "GroundLcd.h"
#include "GroundTelemetry.h"
// Ground peripherals stay outside the shared manager runtime.
namespace endpoint_application {
constexpr LinkManager::Role Role = LinkManager::Role::Ground;
inline void begin(MessageManager& messages) { messages.setReceiver(&ground_telemetry::receiver); }
inline void onPacketActivity(const board_link::PacketActivity& packet) { ground_telemetry::receiver.onPacketActivity(packet); }
inline void pollLink(const LinkManager& link,uint32_t now) {
 const auto signal=link.statistics();
 ground_lcd::updateLink(link.established()?uint8_t(link.dataProfile()):255,signal.hasReceiveMeasurement,signal.lastRssi,signal.lastSnr);
 ground_telemetry::receiver.pollLink(link,now);
}
inline bool outputReady() { return ground_telemetry::receiver.idle(); }
inline void configureLink(LinkManager::Config& config) { config.autoProbe=true; }
inline void pollSensors(uint32_t) {
 if(ground_telemetry::receiver.idle()) { ground_sd::report();ground_lcd::report(); }
 ground_telemetry::receiver.poll();
}
inline void pollData(MessageManager&, uint32_t) {}
inline void beginCore1() { ground_sd::begin();ground_lcd::begin(); }
inline void pollCore1() { ground_lcd::poll(); }
}
