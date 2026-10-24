#pragma once
#include <stdint.h>
// Shared modulation identities only; selection and negotiation remain in LinkManager.
namespace modulation_profiles {
inline uint8_t identify(uint8_t sf,uint32_t bw,uint8_t cr) {
 if(sf==12 && bw==125000UL && cr==8) return 0;
 if(sf==10 && bw==125000UL && cr==6) return 1;
 if(sf==9 && bw==125000UL && cr==5) return 2;
 if(sf==8 && bw==250000UL && cr==5) return 3;
 if(sf==7 && bw==500000UL && cr==5) return 4;
 return 5;
}
}
