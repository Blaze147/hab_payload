# Balloon radio — Stage 1

Reusable nonblocking RadioManager for the existing BSFrance LoRa32U4 II project.

```sh
pio test -e native
./tools/test-native.sh
pio run -e transmitter -e receiver
```

The second command runs host tests with AddressSanitizer/UBSan and requires a
local C++ compiler. Default firmware initializes the radio and stays idle. Hardware test code is
excluded from those builds. Opt-in one-way builds: `transmitter_test` and
`receiver_test`. Optional exchange builds: `transmitter_bidirectional` and
`receiver_bidirectional`. No hardware is needed to build or run native tests.

- [Public API, state rules and simulator](docs/RadioManager.md)
- [Upload and physical test procedure](docs/HARDWARE_TESTING.md)
- [Executed tests, memory usage and pending validation](docs/STAGE1_REPORT.md)

Attach suitable antennas and establish permitted RF settings before hardware
transmission. Physical wireless functionality has not been validated here.
