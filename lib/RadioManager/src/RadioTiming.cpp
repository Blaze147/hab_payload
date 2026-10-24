#include "RadioManager.h"
#include <math.h>
namespace radio {
uint32_t RadioManager::airtimeMs(const Config& c, size_t n) {
 const double symbol = double(1UL << c.spreadingFactor) / c.bandwidth;
 const int de = symbol > 0.016 ? 1 : 0;
 const double numerator = 8.0*n - 4*c.spreadingFactor + 28 + (c.crc ? 16 : 0);
 const double blocks = ceil(numerator / (4*(c.spreadingFactor-2*de)));
 const double payload = 8 + (blocks > 0 ? blocks*c.codingRate : 0);
 return uint32_t(ceil((c.preamble + 4.25 + payload)*symbol*1000));
}
}
