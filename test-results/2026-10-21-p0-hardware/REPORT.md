# Real two-board P0 bench test — 2026-10-21

**Result: the fresh second run passed the basic integrated hardware test.**
Run on the two connected Feather 32u4 boards, using user-confirmed suitable
915 MHz antennas and the port mappings in platformio.ini. The second run's logs
span approximately 21:07–21:11 America/Denver (timestamps within logs are UTC).

| Role | Port | Uploaded firmware |
| --- | --- | --- |
| Ground | /dev/cu.usbmodem1301 | receiver_link_test |
| Balloon | /dev/cu.usbmodem1401 | transmitter_link_test |

Both uploads were flash-verified by avrdude. P0 remained SF12, 125 kHz, CR4/8
at 915 MHz. Automatic profile probing was disabled. Ground was flashed/captured
first, then balloon. No simulated radio adapter was used for these logs.
Firmware image hashes and the base Git revision are in [manifest.json](manifest.json);
the tested source includes uncommitted working-tree changes.

## Actual results, fresh run 2

- Ground received **30/30 counters, 0–29, once and in order**.
- Zero counter sequence errors and zero invalid fixture-payload messages.
- Ground recorded 30 data packets and **240 unique payload bytes**.
- **Zero CRC errors** and zero remaining provisional gaps in this fresh run.
- **Four completed handshake cycles** were observed: balloon reached Complete
  and ground reached Provisional (its final ACK was sent). Ground's provisional
  label correctly does not claim independent proof of final-ACK receipt; the
  balloon log provides that evidence for these observed cycles.
- Twenty counters arrived before the first completed handshake; ten afterward.
- Initial P0 beacons moved ground out of Searching into a scheduled control
  window. Normal data/control operation continued through the later cycles.
- Both radios supplied physical RSSI/SNR metadata. Final ground snapshot was
  RSSI -32 dBm and SNR 10.2 dB; final balloon snapshot was approximately -30 dBm
  and 10.0 dB. These are nearby bench measurements, not a range characterization.
- No Radio poll errors or Fault states appeared in the successful run.

Evidence:

- [Ground serial log](run2/ground.log)
- [Balloon serial log](run2/balloon.log)
- [Parsed checks and counter list](run2/summary.json)

Raw logs are kept locally; repository rules ignore *.log. The Markdown/JSON
report files retain the key results separately.

## First run failed; a stack-footprint fix preceded the successful rerun

The first run did not pass: ground received 19 counters through 19, missing
counter 15, and the balloon's first SYN-ACK transmission timed out. Its log also
showed an anomalous NotInitialized status before the fault. One ground CRC
rejection was already present before test counter zero, during upload transition;
it is not included in the fresh second run's zero-error result.

AVR disassembly showed a **351-byte main stack frame**, with only about 540
bytes available after static allocations. The small counter fixture was creating
a full DataPacket with its 240-byte payload array; link-time inlining kept this
large stack allocation on the receive/control path as well. This left very little
space for nested codec/radio calls and interrupts, making stack exhaustion a
plausible cause. No stack-canary measurement established the exact overwrite.

Added a header-plus-borrowed-payload sendData overload and changed the fixture to
use a small header and eight-byte payload. The compiled main frame decreased to
**139 bytes**, a 212-byte reduction. Both boards were reflashed and counters and
statistics restarted. The second run then completed 30/30 delivery and four
handshakes without the earlier status corruption or timeout. This is evidence
that the fix resolved the observed run, not a formal worst-case stack proof.

Failed-run evidence remains in [run1/ground.log](run1/ground.log) and
[run1/balloon.log](run1/balloon.log).

## What this establishes

On this hardware/configuration, the application fixture, hardware SPI/radio
adapter, physical LoRa TX/RX, packet encoding/CRC checking, decoded Fragment
path, single-fragment MessageManager delivery, receive statistics, beacon
acquisition and repeated P0 handshake all operated together. Data continued
successfully after establishment. This is a short real bench integration test.

It does not establish RF range, interference tolerance, maximum-payload or long
message operation, replay/retransmission storage, recovery under deliberate RF
loss, automatic profile probing, independent device restart behavior, long-run
reliability, or a measured worst-case stack margin. Full-message reconstruction
and unique boot-session allocation remain out of scope.

## Post-fix software checks and memory

The native sanitizer harness and all 21 PlatformIO native groups pass. All eight
firmware variants build. The physical-test images use:

| Firmware | Static SRAM / 2560 | Flash / 28672 |
| --- | ---: | ---: |
| transmitter_link_test | 2039 (79.6%) | 26460 (92.3%) |
| receiver_link_test | 2043 (79.8%) | 26110 (91.1%) |

The boards remain flashed with the test firmware. Counter generation stops at
30; periodic P0 control conversations continue while the boards stay powered.
Serial capture ends independently of that firmware behavior.
