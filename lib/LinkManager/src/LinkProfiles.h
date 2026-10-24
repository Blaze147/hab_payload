#pragma once
#include <stdint.h>
#include <ModulationProfiles.h>

// LinkManager owns profile selection, negotiation and recovery policy.
// These definitions describe modulation only; frequency is not a profile field.
namespace link_profiles {
enum class Id : uint8_t { P0 = 0, P1 = 1, P2 = 2, P3 = 3, P4 = 4 };
struct Definition {
 Id id;
 uint8_t spreadingFactor;
 uint32_t bandwidth;
 uint8_t codingRate; // Denominator: 8 means 4/8.
};
constexpr Definition P0 = {Id::P0, 12, 125000UL, 8};
constexpr Definition P1 = {Id::P1, 10, 125000UL, 6};
constexpr Definition P2 = {Id::P2, 9, 125000UL, 5};
constexpr Definition P3 = {Id::P3, 8, 250000UL, 5};
constexpr Definition P4 = {Id::P4, 7, 500000UL, 5};
// Classify captured modulation without involving RadioManager in named profiles.
inline uint8_t identify(uint8_t sf, uint32_t bw, uint8_t cr) {
 return modulation_profiles::identify(sf,bw,cr);
}
constexpr Id Default = Id::P0;
constexpr Id Fallback = Id::P0;
}
