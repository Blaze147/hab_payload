# Balloon radio communication stack

Default builds now split one device across the BSFrance radio board
(RadioManager + ProtocolManager) and RP2040 Zero (all other managers).
See [split-device wiring, builds and UART behavior](docs/SPLIT_DEVICE.md).
The original transmitter/receiver builds remain available explicitly.

Nonblocking radio, packet codecs, link acquisition/handshake and receive statistics
for the BSFrance LoRa32U4 II project.

```sh
pio test -e native
./tools/test-native.sh
./tools/test-split.sh
pio run                         # split_radio + split_host
pio run -e transmitter -e receiver # legacy single-board builds
```

Both shell test scripts run host tests with AddressSanitizer/UBSan and require a
local C++ compiler. Default firmware runs role-aware acquisition and P0 control handshakes.
Typed sensor messages and bounded multi-fragment transfer are implemented; see
[MessageManager](docs/MessageManager.md). Automatic retransmission remains pending. Hardware test code is
excluded from those builds. Opt-in one-way builds: `transmitter_test` and
`receiver_test`. Optional exchange builds: `transmitter_bidirectional` and
`receiver_bidirectional`. No hardware is needed to build or run native tests.

- [Higher-rate UART hardware results: 100000 baud](test-results/2026-10-22-uart-100000/REPORT.md)
- [Ground station: live balloon P0 reception and handshake](test-results/2026-10-22-ground-listen/REPORT.md)
- [Split-device implementation and live hardware results](test-results/2026-10-22-split/REPORT.md)
- [Integrated P0 counter test: builds, procedure and results](docs/LINK_TEST.md)
- [Receive statistics, logging modes and metric definitions](docs/StatisticsManager.md)
- [Link runtime, handshake and recovery](docs/LinkManager.md)
- [Module responsibilities and architecture](docs/ARCHITECTURE.md)
- [Protocol Manager packet format and limits](docs/ProtocolManager.md)
- [Public API, state rules and simulator](docs/RadioManager.md)
- [Upload and physical test procedure](docs/HARDWARE_TESTING.md)
- [USB unavailable, BOOTSEL upload recovery, and flight watchdog behavior](docs/USB_RECOVERY.md#troubleshooting-usb-unavailable-or-upload-cannot-enter-bootsel)
- [Internal DS18B20 probe, RP2040 timing fix, and diagnostic builds](docs/DS18B20.md)
- [Executed tests, memory usage and pending validation](docs/STAGE1_REPORT.md)

Attach suitable antennas and establish permitted RF settings before hardware
transmission. A short two-board P0 bench test passed; see
[test results](test-results/2026-10-21-p0-hardware/REPORT.md). Range and extended
physical validation remain pending.
