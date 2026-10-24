# Integrated P0 counter test

A real two-board run has now passed after a test-sender stack-footprint fix.
See [the hardware report](../test-results/2026-10-21-p0-hardware/REPORT.md) for
30/30 physical counter delivery and four completed P0 handshake cycles.

This is an opt-in hardware test plus a matching automated two-device simulation.
It exercises the current communication stack using short, single-fragment data.
It does not require large messages, reassembly or a replay cache.

## Test behavior

- Balloon starts the normal 30-second data / 10-second conversation schedule.
- All modulation stays P0; automatic profile probing is disabled on both devices.
- The balloon sends 30 counters (0..29). Each packet has one eight-byte payload:
  ASCII `HAB1` followed by an unsigned 32-bit big-endian counter. Its message ID
  is the counter's low 16 bits; the fixed test session ID is 0xB001. These are
  fixture IDs, not production boot/session allocation.
- Counter increments only after local radio acceptance. Calculated airtime plus
  a one-second gap separates requests. Busy/window-deadline rejection retries
  later without consuming a counter. Full wire length is 23 bytes, including
  the data header and CRC. The normal link data gate remains in force.
- Ground starts P0 searching, so it can receive initial data before acquisition.
  Beacons then align control slots, and the SYN/SYN-ACK/ACK exchange runs on P0.
- Received packet bytes pass through RadioManager, ProtocolManager CRC/decoding,
  Fragment construction, MessageManager's single-fragment path and an application
  counter callback. Source directly uses ProtocolManager::sendData(), explicitly
  bypassing unfinished MessageManager::send()/outbound fragmentation.
- The callback verifies length, magic bytes, session, message ID and counter order.
  Statistics and logs report RX count, CRC errors, useful bytes and signal metadata.
- After counter 29 finishes local TX, data generation stops; control/link activity
  continues so the handshake and subsequent control cycles can still be observed.

Shared fixture code is in `include/LinkTestFixture.h`, used by both the native
integration case and `src/link_test/LinkTestApp.cpp`. The board app does not wait
for USB. Initial status is printed when a monitor connects; transitions and a
summary every ten seconds follow. SNR is printed as SNRx10: 75 means 7.5 dB.
Poll errors are printed on status changes rather than flooding every loop.
No heavy history buffer is allocated.

## Build and run on boards

```sh
pio run -e transmitter_link_test -e receiver_link_test
```

Choose the actual USB ports on the test machine, then upload the ground and
balloon builds respectively, for example:

```sh
pio run -e receiver_link_test -t upload --upload-port /dev/cu.YOUR_GROUND_PORT
pio run -e transmitter_link_test -t upload --upload-port /dev/cu.YOUR_BALLOON_PORT
pio device monitor --port /dev/cu.YOUR_GROUND_PORT --baud 115200
```

Open a second monitor for balloon, also at 115200. Close the relevant monitor
before uploading; bootloader ports can change. Follow the existing antenna,
module-band and RF operating setup checks in HARDWARE_TESTING.md. The placeholder commands above show the operator procedure. Uploads and
a physical run on the actual configured ports are recorded in the hardware report.

Start/reset ground first, then reset balloon. Observe for three minutes:

1. Both show radio initialization OK and Link start result 0.
2. Balloon shows data TX accepted counters and separate local TX completion logs.
   Ground shows matching data RX counters; TX completion alone is not delivery.
3. Balloon's first beacon slot starts around 30 seconds. Ground changes from
   Searching to a scheduled Data phase after receiving a beacon.
4. The first control-listening slot starts around 70 seconds. Ground sends SYN,
   balloon sends SYN-ACK and ground sends ACK. Balloon shows `complete`; ground
   shows `provisional (ACK sent)`, correctly reflecting final-ACK uncertainty.
5. Remaining counters arrive during the data phase after the handshake. Expected
   clean reception: 30 packets, last counter 29, zero invalid payloads/sequence
   errors, 240 unique useful bytes in P0 statistics. Packet totals also include
   beacons/control and therefore exceed 30. Signal values are real board readings.
6. Control exchanges continue after the data counter limit is reached.

Save both logs. Unexpected gaps, CRC errors, faults or missing handshakes are
failures to investigate, not hidden by a local TX-success message. Reset both
boards before a fresh counter run; the fixture's session/counters are fixed.
Use `-D LINK_TEST_PACKET_LIMIT=...` in the two opt-in build flags to change the
limit (0 means unlimited), preserving each role and LINK_TEST flags. This simple
check is not a general reordering/retransmission-aware application receiver.

## Executed software results

The automated test simulates 180 seconds with two real RadioManagers,
ProtocolManagers and LinkManagers, a real receiving MessageManager, and the same
counter source/receiver used by board firmware. The adapter/channel are fake.

Observed output:

```text
Integrated P0 simulation: TX=30 RX=30 pre-link=20 post-link=10 last=29 invalid=0 sequenceErrors=0 usefulBytes=240
```

Assertions passed for:

- All 30 radio-accepted counters delivered once in order, with exact payload and
  decoded message/session identities; none were invalid.
- Actual encoded beacons traversed the channel and aligned a control window;
  actual encoded handshake packets established the link in both local states.
- Twenty data packets arrived before local establishment and ten afterward.
- All data transmissions began in data phases using P0; control/beacon traffic
  and later data did not disrupt this nominal sequence.
- Ground statistics recorded 30 data packets, 30 completed single-fragment
  deliveries and 240 unique payload bytes. Synthetic RSSI -90 dBm and SNR 7.5 dB
  reached statistics correctly; no CRC failures were observed in this clean case.

The integration test initially exposed 29/30 delivery because reapplying an
unchanged P0 setting at a slightly shifted window boundary interrupted an
in-flight packet. LinkManager now avoids reconfiguring unchanged modulation;
the 30/30 assertion is the regression check for that fix.

The full suite passes 21 native groups and 17,239,889 sanitizer-harness checks,
including the existing corruption, loss, duplicate, fallback and clock tests.
All eight firmware environments build, including both new integrated targets.

## What has not been proved

The simulation alone does not validate SPI, IRQs, antennas, range, real signal readings,
RF interference/collisions, physical clock drift, or peak interrupt/stack use.
The clean P0 test does not exercise profile probing, full-message assembly,
retransmission replay or unique boot IDs. Other native cases exercise some
software failure paths, but they are not hardware measurements.

Both new AVR binaries fit. Static RAM and flash usage are reported by the build;
these figures exclude peak stack and do not guarantee physical runtime stability.
See the current build figures below.

| Build | Static SRAM / 2,560 | Flash / 28,672 |
| --- | ---: | ---: |
| transmitter_link_test | 2,039 (79.6%) | 26,460 (92.3%) |
| receiver_link_test | 2,043 (79.8%) | 26,110 (91.1%) |

The native test does not allocate within production managers, but simulator
adapters use host containers. Board firmware includes neither simulator code
nor a dense history buffer. Hardware stack headroom remains a test requirement.


The board fixture now uses a small decoded header plus a borrowed eight-byte
payload when sending. The compatibility DataPacket overload remains available.
This avoids a full 255-byte logical packet allocation in the small AVR's main
stack frame. The hardware report documents the failed first run and measured
compiler frame-size reduction; peak stack remains unmeasured.
