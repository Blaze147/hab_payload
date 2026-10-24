# BMP280 shared-I2C identity check

Flashed the full balloon RP2040 firmware with PressureProbe.h read-only
identity diagnostics. Normal application remains running. Internal DS18B20
troubleshooting remains paused; its configured pin stays GP27.

- Shared I2C1/Wire1: SDA GP2, SCL GP3, 100 kHz, existing cross-core bus mutex.
- BMP280 responds at 0x76. Register 0xD0 returns 0x58, the BMP280 chip ID.
- 4 repeated identity checks succeeded with i2c_result=0.
- Alternate 0x77 did not respond; the RP2040 Wire API reported result 4 there.
  The detected module at 0x76 is responding successfully.
- 16 GPS reports retained zero I2C errors and zero checksum failures.
- Camera reports CAPTURED, SPI check passed, OV2640 sensor ID 0x2641.
- Balloon release build and upload/flash verification passed.
- git diff --check passed.

The diagnostic writes only the chip-ID register pointer, then reads its value.
It does not configure pressure conversions, calculate pressure or add pressure
to LoRa telemetry. Pressure measurement integration remains separate work.

Reference: https://raw.githubusercontent.com/adafruit/Adafruit_BMP280_Library/master/Adafruit_BMP280.h
