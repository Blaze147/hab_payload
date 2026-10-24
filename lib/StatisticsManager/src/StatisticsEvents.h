#pragma once
#include <stdint.h>
namespace packet_statistics {
constexpr uint8_t UnknownProfile = 5;
enum class Integrity : uint8_t { Valid, Corrupt, Malformed, Unchecked };
 struct Observation {
  uint32_t receivedAtMs = 0;
  uint16_t wireBytes = 0;
  uint32_t airtimeMs = 0; // Observed RF attempt; zero means unavailable.
  int16_t rssi = 0;
  float snr = 0;
  uint8_t profile = UnknownProfile;
  Integrity integrity = Integrity::Unchecked;
  bool crcChecked = false;
  bool data = false;
  uint16_t sessionId = 0, messageId = 0;
  uint16_t fragmentIndex = 0, fragmentCount = 0;
  uint8_t payloadBytes = 0, retryCount = 0;
 };
class Sink {
public:
 virtual ~Sink() = default;
 virtual void observe(const Observation&) = 0;
 virtual void recordRadioError() = 0;
};
}
