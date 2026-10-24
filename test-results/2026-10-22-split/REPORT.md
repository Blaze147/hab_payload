# Split-device validation — 2026-10-22

The project now builds one endpoint as a BSFrance LoRa32U4 II radio coprocessor
and a Waveshare RP2040 Zero application/policy host. Both connected boards have
been programmed with the production firmware, using balloon policy on the RP2040.

## Hardware identification and wiring

| Device | Observed identity | Running USB port |
| --- | --- | --- |
| BSFrance LoRa32U4 II | VID:PID 239A:800C, Feather 32u4, location 0-1.3 | /dev/cu.usbmodem1301 |
| RP2040 Zero | VID:PID 2E8A:0003, serial 5303284748B66A1C, location 1-1 | /dev/cu.usbmodem1101 |

The RP2040 initially had no running serial device. It subsequently appeared as
RPI-RP2 (bootloader v3.0); its first firmware was loaded by copying the test UF2.
Later uploads used picotool through USB and verified flash. The AVR was uploaded
with avr109/avrdude; signature 0x1e9587 and flash verification succeeded.

Existing wiring was retained: RP2040 GP0/TX to BSFrance D0/RX, GP1/RX to D1/TX,
and common ground. Live communication confirmed both directions at 38400 baud.

## Simulated hardware and software tests

- `pio test -e native`: 21 test cases passed.
- `./tools/test-native.sh`: 17,239,889 checks passed, including the existing P0
  integration and 1000 alternating radio transfers, under ASan/UBSan.
- `./tools/test-split.sh`: 335 checks passed under ASan/UBSan and warnings as
  errors. The production UART parser, RadioServer, RemoteProtocol and managers
  run against finite 64-byte serial buffers paced at 261 microseconds per byte
  and the existing simulated physical radio/channel.
- Two complete split endpoints, with four independent clock offsets, acquired,
  completed SYN/SYN-ACK/ACK and negotiated/applied P1 after P0 acquisition.
- Maximum 255-byte data packets and maximum control bitmaps, escaping,
  metadata/profile forwarding, hardware CRC errors, malformed protocol packets,
  invalid configurations, backpressure, closed/expired data windows, UART
  corruption, outstanding-command cancellation after an event gap, lost host
  lease, millis wrap, independent radio reboot and MessageManager delivery passed.
- Legacy transmitter/receiver firmware also compiled after the interface refactor.

See [native results](native-regression.log), [PlatformIO results](platformio-native.log)
and [split simulation results](split-simulation.log).

## Live diagnostics

Final opt-in diagnostic run: **170 checks passed, zero failed**.

- 32 distinct maximum-size packets were injected into the actual radio-board
  ProtocolManager decoder and returned over the physical UART to the RP2040.
  MessageManager delivered all 32 application messages with all 240 payload bytes
  verified and zero invalid messages. This tests the decoder and board boundary;
  these receptions were synthetic, not received over RF.
- Corrupt and malformed packets were rejected and counted by the RP2040
  StatisticsManager. The profile bucket recorded the expected 34 observations.
- Data-window expiry prevented a send.
- Ten actual local radio transmissions completed, reported across UART.
  No second RF receiver was connected, so completion does not establish delivery.
- 100 receive commands completed; P4 test configuration and P0 restoration worked.
- Host reported zero timeouts, malformed UART frames, event gaps, queue overflows
  and radio errors in the final fresh-boot test.
- AVR stack watermark retained at least **288 bytes untouched** during tested
  diagnostic paths, with 1934 bytes static SRAM. This is an observed test margin,
  not proof of worst-case stack usage in all possible interrupt paths.

The initial diagnostic attempt failed two checks. Startup included a missing radio
while its firmware was being installed, and a test could encounter a transmission
already in progress. The harness now compares transport counters against its
starting baseline and waits for an existing TX to finish. Subsequent runs passed;
the initial log is retained rather than discarded.

See [final diagnostic log](hardware-selftest-final.log) and
[initial diagnostic log](hardware-selftest.log). The radio's two pre-existing bad
frames in the final diagnostic log occurred across deliberate host reprogramming;
that counter did not increase during the diagnostic run.

## Independent restart recovery

The radio board was deliberately reprogrammed while the RP2040 stayed running.
The host detected missing replies, faulted the remote service, retried while the
AVR bootloader was active, then reported RADIO_READY and resumed a healthy UART
connection without a host reset. The four timeout counts in this experiment were
expected during that outage. The RP2040 was subsequently reprogrammed separately
and established a fresh session with the already running radio board.

See [radio restart observed by host](radio-restart-host.log) and the upload logs.

## Production builds installed

| Environment | Static SRAM | Flash |
| --- | ---: | ---: |
| split_radio | 1932 / 2560 bytes | 21274 / 28672 bytes |
| split_host | 16568 / 262144 bytes | 53556 / 2093056 bytes |
| split_host_ground (also built) | 16568 / 262144 bytes | 53556 / 2093056 bytes |

Both production binaries were flashed and verified after diagnostics. Symbol
inspection confirms that the radio binary excludes StatisticsManager, LinkManager
and MessageManager implementations; the host excludes ProtocolManager and the
RadioManager hardware runtime. Only the shared airtime formula is present on the
host. Test injection is absent from production firmware.

See [build log](builds.log), [binary ownership and hashes](binary-ownership.log),
[radio production upload](radio-production-upload.log) and
[host production upload](host-production-upload.log).

## Production observation

A **95-second** observation of the installed production firmware captured 48 host
status samples and all three active phases: data, beacon and control-listen.
The test reported **zero failures**, with zero host radio errors, timeouts,
malformed UART frames, event gaps or overflows. The radio-side bad-frame and
rejected-command counters remained zero. P0 beacon transmissions and subsequent
state changes were observed. Established remained false, as expected without a
second physical endpoint.

See [production observation log](production-soak.log).

## Scope limits

Only one physical endpoint (the two connected microcontrollers) was available.
Two-endpoint RF reception, hardware handshake/profile negotiation, range and
sustained high-rate P4 reception remain unverified on physical hardware. They
were exercised in the native split simulation where applicable. UART rate and
clock synchronization should be measured under the final application's traffic.

The existing unfinished outbound fragmentation, multi-fragment reassembly and
replay cache were not implemented as part of this processor split. USB `x` stops
link scheduling; `r` restarts it. The installed production host is left running
normal balloon acquisition, which can transmit P0 beacons.
