#pragma once
#include <PacketService.h>
#include <ModulationProfiles.h>

namespace telemetry {
// Idle pause after estimated TX completion; unknown modulation uses P0 pacing.
inline uint16_t pauseMs(const radio::Config& config) {
 const uint8_t id=modulation_profiles::identify(config.spreadingFactor,config.bandwidth,config.codingRate);
 const uint16_t pauses[]={2000,1000,500,250,125};
 return pauses[id<5 ? id : 0];
}
}
