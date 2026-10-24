# MessageManager refactor verification

Source-only refactor; no hardware upload was performed. The balloon remains on
the earlier shared-bus/MAX31865/temperature-offset firmware.

Implemented:
- Typed MessageCodec inside MessageManager library, numeric sensor identities,
  signed hundredths-C temperatures, DHT humidity, pressure, GPS and camera API.
- Manager-owned session/message lifecycle, one bounded outbound queue, packet
  fragmentation, out-of-order assembly, duplicate/conflict handling and timeout.
- 32784-byte protocol message limit, 240-byte packet payload limit unchanged.
- Balloon typed producer includes corrected external RTD; unconnected sensors
  stay absent. Ground receives typed callbacks; Python exposes separate fields.
- Automatic camera RF sending, ground image storage/display and automatic
  retransmission/replay are not enabled/implemented by this refactor.

Passed:
- split_radio, split_host and split_host_ground release builds.
- Native tests with AddressSanitizer/UBSan: 17270630 checks, including 32 KiB
  synthetic camera message through packet codecs/radio simulation, malformed
  records, cold temperatures, reversed fragments, loss, duplicate/conflict,
  timeout/millis wrap and established-link telemetry pacing.
- Split tests with AddressSanitizer/UBSan: 1375 checks, including complete
  1000-byte message across simulated RF plus both byte-paced UART connections,
  delayed by a closed data gate, with no UART overflow or transport timeouts.
- 30 Python tests, including C++ golden bytes, distinct DHT/external RTD fields,
  unavailable sensors and malformed messages; legacy HABT-v1 decode retained.
- git diff --check.

Limitation: legacy single-AVR transmitter does not fit its 28672-byte flash
limit. Reconstructed original MessageManager/512-byte-limit build: 34514 bytes;
new manager: 37356 bytes. Split deployments build successfully. This was an
existing size failure, although the new manager adds code to that legacy target.

No two-endpoint physical LoRa verification or cold calibration was performed.
Deploy matching host firmware; both radio coprocessors need the extended size
validator before using messages larger than the old 512-byte limit.

Architecture and wire format: docs/MessageManager.md.
