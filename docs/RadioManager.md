# Stage 1 radio API

The Arduino-independent core is in `lib/RadioManager/src`. The physical adapter is
`include/PhysicalRadioAdapter.h`. Permanent host simulation is in
`test/test_native/Simulation.h`; future managers can reuse it without Arduino.
Both firmware roles use exactly the same manager implementation.

## Configuration

Config defaults define provisional P0: 915 MHz, SF12, 125 kHz bandwidth,
4/8 coding, 17 dBm PA_BOOST, eight-symbol preamble, CRC enabled, sync word 0x12,
explicit header and normal IQ. P1 uses SF9 and 4/5; other settings match P0.
These are test profiles, not measured claims of optimal reliability.
The original code specified only 915 MHz; it relied on library defaults for
modulation. Those defaults are deliberately replaced by explicit P0 settings
and CRC for this stage. Original pins CS8/reset4/DIO0=7 are preserved.

Frequency validation is deliberately scoped to this project's US 902–928 MHz
band. SF7–12, coding denominators 5–8, power 2–20 dBm and preamble >=6 are
accepted. SF6 requires implicit headers and is unsupported. Bandwidth must be
one of the ten exact driver choices documented in `valid()`; values are not
silently rounded. Validation is a capability check, not authorization to emit RF.

## Usage and state contract

Construct `RadioManager manager(adapter)` with an adapter reference that outlives
it. Use one physical adapter/manager pair per board (the library has a singleton).
Call `initialize(config)` and check its result. Successful initialization leaves
Idle; it also provides explicit recovery from Fault. Invalid configurations leave
existing state and settings untouched. Reinitializing while transmitting is Busy.
Initialization may briefly block for the library's hardware reset; packet
transmission, reception, and completion polling do not wait for airtime.

`transmit(bytes, length, nowMs)` accepts 1–255 arbitrary bytes; null pointers,
zero length and oversized payloads are InvalidPayload. A successful request
copies into the adapter/FIFO before returning, so caller memory can immediately
be reused. It switches Receiving to Transmitting, discarding an unread packet.
During transmission, initialize/configure/idle/receive/read/transmit return Busy.
Call `poll(nowMs)` regularly with a monotonic unsigned 32-bit millisecond clock.
InProgress becomes Complete only after local completion, then state becomes
Idle. Complete remains visible until the next request or initialization.
It says nothing about remote reception, even when the simulator drops a packet.
Timeout is twice modeled airtime plus one second; clock rollover is supported.

Call `receive()` to listen and `read(buffer, capacity, info)` repeatedly. A
successful read supplies exact bytes, length, RSSI (dBm), SNR (dB), and leaves the
manager Receiving. NoPacket means nothing is available. BufferTooSmall reports
the required length in info without writing or consuming the pending packet.
Calling idle explicitly discards a pending packet. At most one packet is retained;
new arrivals are dropped while one is pending. On physical hardware a pending
packet is held in the FIFO in standby; successful read rearms continuous RX.
Applications must service RX promptly. There are unavoidable gaps during reads,
configuration and state changes. No software packet queue or manager payload
buffer exists. The application owns its fixed 255-byte buffer.

Configuration is validated in full before touching the adapter. When Receiving,
configure stops RX, discards pending data, applies all settings and resumes RX.
During Idle it stays Idle. Driver failures can leave partially applied hardware
settings: the manager enters Fault and requires successful reinitialization.
HardwareError and Timeout attempt standby and set TX status Failed. Fault
rejects normal operations. Recovery retry timing and serial logging belong to
the optional hardware test application; its retries occur every five seconds without heap allocation.

Status values: Ok, NotInitialized, InvalidConfig, InvalidPayload, Busy, NoPacket,
BufferTooSmall, HardwareError, Timeout. `statusName()` supplies printable names.
Inspect state() and transmission() for logical state and latched local TX result.
Payload and PacketInfo are meaningful only for successful reads, except the
required size/metadata returned with BufferTooSmall.

## Physical driver bridge and limitations

LoRa 0.8.0 is pinned to the inspected version. `endPacket(true)` starts async TX,
but its `isTransmitting()` method is private. The adapter polls SX1276 IRQ_FLAGS
(0x12, TX_DONE 0x08), checks VERSION (0x42, expected 0x12), and clears TX_DONE
using the same SPI bus/settings and chip-select as LoRa. RX_DONE gates calls to
parsePacket so empty polls do not switch the chip from continuous RX to single RX.
No custom ISR or dependency on DIO0 interrupt wiring is introduced. Recheck this
bridge if upgrading the library. Configuration setters return void; their success
cannot be verified by the public library. Version checks detect some bus/device
failures; arbitrary RF failures cannot be detected. CRC-corrupted/empty RX packets
are rejected, but the public parse API cannot distinguish the two, so they report
InvalidPayload when observed. Nothing here proves physical RF functionality.

## Deterministic simulator

Channel.advance(ms) advances a shared clock explicitly. Fake adapters expose
availability, configuration/TX failure and stuck-TX injection, drop policy and
RSSI/SNR. Delivery occurs when airtime expires; the receiver must have been
listening with identical settings from the start and must not change mode/settings
in the interim. Power is not a compatibility constraint. Matching preamble lengths
are a conservative simulator rule. Only one pending packet is kept. Adapters and
channel must outlive in-flight transmissions. Do not copy adapters.

Airtime approximates explicit-header LoRa with configured CRC, preamble, SF,
bandwidth and coding, and automatic low-data-rate optimization for symbols longer
than 16 ms. It is functional timing, not a cycle-accurate driver model. There is no
propagation/link budget, capture, collision/interference or antenna model. Multiple
transmitters aimed at one receiver are not modeled realistically. Extend the
permanent simulator when later stages need those cases. Simulation uses host
vectors; no simulation headers or heap allocations enter firmware.

## Application separation

`src/main.cpp` is a small entry point. The default transmitter/receiver firmware
initializes the manager and stays idle, with no test transmissions, receive
printing or test recovery policy. It is a Stage 1 foundation, not the future
telemetry application. `src/hardware_test/HardwareTestApp.cpp` contains all test
payload generation, intervals, diagnostics, exchange sequencing and retries.
Only explicit test environments include that source via build_src_filter and
define HARDWARE_TEST. Native tests remain separate from every embedded build.
