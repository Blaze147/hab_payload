#pragma once
#include <stdint.h>
#include <stddef.h>
#include <RadioManager.h>
#include <StatisticsEvents.h>

namespace protocol {
constexpr uint8_t Version = 1;
// Wire offsets and widths in bytes. Multi-byte integers are big-endian.
constexpr size_t VersionOffset = 0;
constexpr size_t TypeOffset = 1;
constexpr size_t CommonHeaderSize = 2;
constexpr size_t TransmitterSessionIdOffset = 2; // uint16_t, sender's boot session.
constexpr size_t MessageIdOffset = 4; // uint16_t.
constexpr size_t FragmentCountOffset = 6; // uint16_t, fragments in the message.
constexpr size_t FragmentIndexOffset = 8; // uint16_t, zero-based.
constexpr size_t RetryCountOffset = 10; // uint8_t, 0 for first attempt.
constexpr size_t FlagsOffset = 11; // uint8_t; bit 7 is RestartFlag.
constexpr size_t PayloadLengthOffset = 12; // uint8_t.
constexpr size_t HeaderSize = 13;
constexpr size_t PayloadOffset = HeaderSize;
constexpr size_t CrcSize = 2;
constexpr size_t PacketOverhead = HeaderSize + CrcSize;
constexpr size_t MaxPacketSize = 255; // RadioManager/SX127x packet limit.
constexpr size_t MaxPayloadSize = MaxPacketSize - PacketOverhead;
constexpr size_t MaxMessageSize = 32784; // 32 KiB JPEG plus typed envelope.
constexpr uint16_t MaxFragmentCount = (MaxMessageSize + MaxPayloadSize - 1) / MaxPayloadSize;

// CRC-16/CCITT-FALSE: width=16, poly=0x1021, init=0xFFFF,
// refin=false, refout=false, xorout=0x0000; "123456789" -> 0x29B1.
// Covers header followed by exactly payloadLength bytes (excludes CRC).
// CRC follows payload immediately, high byte first; no padding on wire.
constexpr uint16_t CrcPolynomial = 0x1021;
constexpr uint16_t CrcInitial = 0xFFFF;
constexpr uint16_t CrcXorOut = 0x0000;
constexpr bool CrcReflectInput = false;
constexpr bool CrcReflectOutput = false;
constexpr uint16_t CrcCheck = 0x29B1;

// All three packet families use version 1 and the same two-byte prefix.
enum class PacketType : uint8_t { Data = 0, Beacon = 1, Control = 2 };
// Bit 7 in existing data/control flags fields. Beacons have no flags field.
constexpr uint8_t RestartFlag = 0x80;
constexpr bool restartFlag(uint8_t flags) { return (flags & RestartFlag)!=0; }
struct BeaconPacket {
 uint8_t version = Version;
 PacketType type = PacketType::Beacon;
 uint16_t timeToNextConversation = 0; // Unsigned units of 100 ms, big-endian.
 uint8_t conversationDuration = 10; // Whole seconds; provisional default.
 uint16_t crc = 0;
};
constexpr size_t BeaconTimeToNextConversationOffset = 2;
constexpr size_t BeaconConversationDurationOffset = 4;
constexpr size_t BeaconHeaderSize = 5;
constexpr size_t BeaconCrcOffset = BeaconHeaderSize;
constexpr size_t BeaconPacketSize = BeaconHeaderSize + CrcSize;
constexpr uint32_t beaconCountdownMs(uint16_t value) { return uint32_t(value) * 100; }
// Structural checks only; raw codecs compute/check CRC and LinkManager owns countdown policy.
bool valid(const BeaconPacket&);
bool valid(const BeaconPacket&, size_t wireLength);
// Handshake markers in the existing one-byte control flags field.
// SYN-ACK combines the SYN and ACK bits; no wire bytes are added.
enum class ControlFlag : uint8_t { None = 0x00, Syn = 0x01, Ack = 0x02, SynAck = 0x03 };
struct ControlPacket {
 uint8_t version = Version;
 PacketType type = PacketType::Control;
 // Reset to 0 at conversation-window end; first active conversation uses 1.
 // Advance on failed-exchange restart
 // within that window to distinguish a fresh conversation from the old one.
 // First packet arriving very early AND with ID >1 signals a likely early
 // ground window; proposed response is to delay the ground window.
 // packet loss can also cause it. See LinkManager.md for the configurable heuristic.
 // LinkManager implements scheduling, attempts and no-wrap exhaustion.
 uint8_t conversationId = 0;
 uint8_t flags = uint8_t(ControlFlag::None);
 // Ground proposes next balloon data profile; balloon echoes acceptance.
 // Always an absolute ID: current ID means keep it, 0 means P0.
 uint8_t profileChangeRequest = 0;
 // Balloon -> ground only: ground adjusts its window by value * 10 ms.
 // Target first-packet timing is after a 50 ms quiet guard; ground starts.
 // Positive delays ground; negative advances the next boundary (see LinkManager).
 // Unused in ground -> balloon packets; byte remains present. Zero = 0 ms.
 int8_t windowTimingSlide = 0;
 uint16_t retransmitMessageId = 0; // Full target data message ID, big-endian.
 // Request selects groups; response echoes acceptance or zero if unavailable.
 uint16_t retransmitGroupBitmap = 0;
 // One 16-bit bitmap per selected group, in ascending group order.
 // Group bit g covers fragments 16*g through 16*g+15 (LSB first).
 uint16_t retransmitBitmaps[16] = {};
 uint16_t crc = 0;
 // Local metadata only, never included in the wire header.
 uint8_t retransmitBitmapCount = 0;
};
constexpr size_t ControlConversationIdOffset = 2;
constexpr size_t ControlFlagsOffset = 3;
constexpr size_t ControlProfileChangeRequestOffset = 4;
constexpr size_t ControlWindowTimingSlideOffset = 5;
constexpr size_t ControlRetransmitMessageIdOffset = 6;
constexpr size_t ControlRetransmitGroupBitmapOffset = 8;
constexpr size_t ControlHeaderSize = 10;
constexpr size_t ControlBitmapsOffset = ControlHeaderSize;
constexpr size_t ControlPacketOverhead = ControlHeaderSize + CrcSize;
constexpr size_t MaxControlBitmapCount = 16;
constexpr size_t controlPacketSize(size_t bitmapCount) { return ControlPacketOverhead + 2 * bitmapCount; }
constexpr size_t controlCrcOffset(size_t bitmapCount) { return ControlBitmapsOffset + 2 * bitmapCount; }
constexpr int16_t windowTimingSlideMs(int8_t value) { return int16_t(value) * 10; }
uint8_t controlBitmapCount(uint16_t groupBitmap);
// Structural checks include group/count consistency; no CRC or profile policy checks.
bool valid(const ControlPacket&);
bool valid(const ControlPacket&, size_t wireLength);
struct Header {
 uint8_t version = Version;
 PacketType type = PacketType::Data;
 // Assigned by the sender at boot; always identifies the transmitting side.
 uint16_t transmitterSessionId = 0;
 uint16_t messageId = 0;
 uint16_t fragmentCount = 1;
 uint16_t fragmentIndex = 0;
 uint8_t retryCount = 0;
 uint8_t flags = 0;
 uint8_t payloadLength = 0;
};
// Logical storage only: never transmit sizeof(Packet) or cast it to wire bytes.
// Native padding/alignment is not part of the protocol. No heap allocation.
struct DataPacket {
 Header header;
 uint8_t payload[MaxPayloadSize] = {};
 uint16_t crc = 0;
};
using DataHeader = Header;
using Packet = DataPacket; // Preserve the existing data-packet API.
constexpr size_t packetSize(size_t payloadLength) { return PacketOverhead + payloadLength; }
constexpr size_t crcOffset(size_t payloadLength) { return PayloadOffset + payloadLength; }
bool validMessageSize(size_t length);
// Structural checks only: does not verify CRC or inspect payload bytes.
// Data messages are nonempty. All non-final fragments must be full; the
// final fragment must keep the implied message size <= MaxMessageSize.
bool valid(const Header&);
bool valid(const Header&, size_t wireLength);
static_assert(MaxPayloadSize <= UINT8_MAX, "Payload length must fit one byte");
static_assert(MaxFragmentCount <= UINT16_MAX, "Fragment count must fit two bytes");
static_assert(packetSize(MaxPayloadSize) == MaxPacketSize, "Packet budget mismatch");
}

namespace protocol {
enum class Result : uint8_t { Ok, InvalidArgument, NotConnected, NotImplemented, Busy, NotReady, HardwareError };
// Owned bytes and decoded data header. No heap; callback lifetime is borrowed.
struct Fragment {
 Header header;
 radio::PacketInfo reception;
 uint8_t profile = packet_statistics::UnknownProfile; // Local metadata, not wire.
 uint8_t rawBytes[MaxPacketSize] = {};
 size_t rawLength = 0;
 const uint8_t* payload() const { return rawBytes + PayloadOffset; }
 size_t payloadLength() const { return header.payloadLength; }
};
uint16_t crc16(const uint8_t* bytes, size_t length);

// Borrowed arguments: valid only during the callback. Copy anything retained.
class DataReceiver {
public:
 virtual ~DataReceiver() = default;
 virtual Result onFragment(const Fragment&) = 0;
};
class LinkReceiver {
public:
 virtual ~LinkReceiver() = default;
 virtual Result onControl(const ControlPacket&, const radio::PacketInfo&) = 0;
 virtual Result onBeacon(const BeaconPacket&, const radio::PacketInfo&) = 0;
};
}

