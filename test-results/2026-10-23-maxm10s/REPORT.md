# MAX-M10S primary GPS replacement — 2026-10-23

Balloon split_host firmware built, uploaded, flash verified and left running.
GPS now uses Wire/I2C0: GP4 SDA, GP5 SCL, 100 kHz, address 0x42.
Camera remains on Wire1/I2C1 GP2/GP3. DHT22 remains on GP7.
Removed UART GPS initialization and GP6 PPS interrupt. Qwiic does not carry PPS;
USB reports PPS as unavailable. MAX-M10S default NMEA is parsed with TinyGPSPlus.

The 32-byte HABT v1 encoder in TelemetryMonitor.h was not changed. GPS location,
altitude and satellites now originate from MAX-M10S; temperature/humidity and
validity behavior remain intact. Speed/UTC remain USB-only. No ground firmware
update is required.

Validation:
- PlatformIO split_host build/upload succeeded.
- GPS map/telemetry tests: 25 passed.
- Split-device simulation: 361 checks passed.
- Integrated hardware capture: integrated.log. Valid fix with 12 satellites,
  zero GPS checksum failures and zero I2C errors observed. Environmental readings
  continue; TELEMETRY TX_ACCEPTED confirms local radio acceptance only.
- Ground station reception has not been verified in this run; user will test it.

comparison.log and diagnostic.cpp are the earlier temporary two-receiver test
on GP14/GP15, before the production wiring change. integrated.log is the current
production GP4/GP5 test.
