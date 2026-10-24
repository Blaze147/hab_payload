# Balloon shared-bus hardware verification

Flashed split_host on RP2040 Zero USB serial 5303284748B66A1C, /dev/cu.usbmodem1201.
Build and flash verification passed.

- PT100 two-wire, MAX31865 CS GP13, shared SPI1 GP10/11/12.
- 69 temperature samples: 25.19–25.33 C / 77.35–77.59 F.
- All samples fault=0x00; no RTD read errors.
- Assumes 100-ohm nominal PT100 and 430-ohm breakout reference resistor.
- 68 GPS reports with zero I2C errors and zero checksum failures on shared GP2/GP3. GPS reports NO_FIX with zero satellites during indoor test; NMEA/UTC stream is working.
- Camera frames 1, 2 passed length, CRC32 and JPEG marker checks while RTD sampling ran.
- RTD USB output is separate from existing DHT22 telemetry; this is a room-temperature plausibility check, not precision calibration.
