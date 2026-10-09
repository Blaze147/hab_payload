#pragma once
#include <stdint.h>
#include <stddef.h>
namespace radio {
constexpr size_t MaxPayload = 255;
enum class Status : uint8_t { Ok, NotInitialized, InvalidConfig, InvalidPayload, Busy, NoPacket, BufferTooSmall, HardwareError, Timeout };
enum class State : uint8_t { Uninitialized, Idle, Receiving, Transmitting, Fault };
enum class TxStatus : uint8_t { None, InProgress, Complete, Failed };
struct Config {
 uint32_t frequency = 915000000UL;
 uint32_t bandwidth = 125000UL;
 uint8_t spreadingFactor = 12;
 uint8_t codingRate = 8;
 int8_t power = 17;
 uint16_t preamble = 8;
 bool crc = true;
 uint8_t syncWord = 0x12;
};
struct PacketInfo { size_t length = 0; int16_t rssi = 0; float snr = 0; };
// All adapters use explicit headers, normal IQ, PA_BOOST. No remote delivery status.
class Adapter {
public:
 virtual ~Adapter() = default;
 virtual bool begin(const Config&) = 0;
 virtual bool configure(const Config&) = 0;
 virtual bool idle() = 0;
 virtual bool receive() = 0;
 virtual bool transmit(const uint8_t*, size_t) = 0;
 virtual TxStatus pollTx() = 0;
 // Copies only if capacity suffices; BufferTooSmall preserves the pending packet.
 virtual Status read(uint8_t*, size_t, PacketInfo&) = 0;
};
class RadioManager {
public:
 explicit RadioManager(Adapter& adapter) : adapter_(adapter) {}
 static bool valid(const Config&);
 Status initialize(const Config&); // Also the explicit recovery operation.
 Status configure(const Config&);
 Status idle();
 Status receive();
 Status transmit(const uint8_t*, size_t, uint32_t nowMs);
 Status poll(uint32_t nowMs); // Call regularly. Wrap-safe unsigned millisecond clock.
 Status read(uint8_t*, size_t, PacketInfo&);
 State state() const { return state_; }
 TxStatus transmission() const { return tx_; }
 const Config& config() const { return config_; }
 static uint32_t airtimeMs(const Config&, size_t);
private:
 Status ready() const;
 Status fault(Status);
 Adapter& adapter_;
 Config config_;
 State state_ = State::Uninitialized;
 TxStatus tx_ = TxStatus::None;
 uint32_t started_ = 0, timeout_ = 0;
};
const char* statusName(Status);
}
