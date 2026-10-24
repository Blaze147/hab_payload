#pragma once
#include "GpsMonitor.h"
#ifndef BENCH_GPS_ONLY
#include "EnvironmentMonitor.h"
#include "CameraMonitor.h"
#include "RtdMonitor.h"
#include "InternalTemperatureMonitor.h"

#include "PressureMonitor.h"
#endif
#include "TelemetryMonitor.h"
#include <pico/rand.h>
#ifdef APPLICATION_WATCHDOG
#include "ApplicationWatchdog.h"
#define BALLOON_WATCHDOG_STAGE(x) application_watchdog::stage(x)
#else
#define BALLOON_WATCHDOG_STAGE(x) ((void)0)
#endif

// Balloon-only peripherals and telemetry producer. Shared managers are supplied
// by the host runtime; this application does not own the link schedule.
namespace endpoint_application {
constexpr LinkManager::Role Role = LinkManager::Role::Balloon;
inline void begin(MessageManager&) {
 gps_monitor::begin();
}
inline void onPacketActivity(const board_link::PacketActivity&) {}
inline void pollLink(const LinkManager&,uint32_t) {}
inline bool outputReady() { return true; }
inline void configureLink(LinkManager::Config&) {}
inline void pollSensors(uint32_t now) {
 BALLOON_WATCHDOG_STAGE(21);
 gps_monitor::poll(now);
#ifndef BENCH_GPS_ONLY
 BALLOON_WATCHDOG_STAGE(22);
 pressure_monitor::poll();
 BALLOON_WATCHDOG_STAGE(23);
 environment_monitor::poll();
 BALLOON_WATCHDOG_STAGE(24);
 rtd_monitor::poll();
 BALLOON_WATCHDOG_STAGE(25);
 internal_temperature_monitor::poll();
 BALLOON_WATCHDOG_STAGE(26);
 camera_monitor::poll();
#endif
}
inline void pollData(MessageManager& messages, uint32_t now) {
 telemetry::poll(messages,now);
}
inline void beginCore1() {
#ifndef BENCH_GPS_ONLY
 BALLOON_WATCHDOG_STAGE(101);
 environment_monitor::beginSensor();
 BALLOON_WATCHDOG_STAGE(102);
 camera_monitor::begin();
 BALLOON_WATCHDOG_STAGE(103);
 rtd_monitor::beginSensor();
 BALLOON_WATCHDOG_STAGE(104);
 internal_temperature_monitor::beginSensor();
 BALLOON_WATCHDOG_STAGE(105);
 pressure_monitor::beginSensor();
#endif
}
inline void pollCore1() {
#ifndef BENCH_GPS_ONLY
 BALLOON_WATCHDOG_STAGE(201);
 environment_monitor::readSensor();
 BALLOON_WATCHDOG_STAGE(202);
 camera_monitor::pollSensor();
 BALLOON_WATCHDOG_STAGE(203);
 rtd_monitor::readSensor();
 BALLOON_WATCHDOG_STAGE(204);
 internal_temperature_monitor::readSensor();
 BALLOON_WATCHDOG_STAGE(205);
 pressure_monitor::readSensor();
#endif
}
}
#undef BALLOON_WATCHDOG_STAGE
