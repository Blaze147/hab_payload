# Ground LCD demo — 2026-10-22

User wiring: LCD SDA to GP2, SCL to GP3; LCD VCC confirmed 3.3V,
with direct signal connections. RP2040 serial 503359276A727B9F detected at
/dev/cu.usbmodem1201. Ground firmware built, uploaded, and flash verified.

Detected exactly one PCF8574-range device, address 0x27, on Wire1 at 100 kHz.
Initialized the standard SunFounder PCF8574-to-HD44780 mapping and sent:

- Row 1: GROUND STATION
- Row 2: Hello from the ground station! (350 ms scroll step)

Serial observation: scrolling frames increased from 15 to 29 to 42;
I2C errors remained zero. SD mount/write/read/verify/delete also passed
for 1024 bytes. See lcd-scroll.log and upload.log.

Visual confirmation is pending: I2C ACKs verify accepted backpack writes,
not LCD pixels. Adjust contrast if text is not visible. Do not raise LCD
VCC to 5V with direct GPIO connections; use I2C level translation first.

All display bus work runs on core 1 alongside the ground SD initialization.
The shared radio manager runtime remains on core 0; radio UART is still
unconnected with the LoRa board's preexisting legacy test firmware.

Reference: https://docs.sunfounder.com/projects/davinci-kit/en/latest/1.1.7_i2c_lcd1602.html

## Garbled visible text: fixed-pattern diagnostic

User confirmed visible but unreadable scrolling characters after a full power
cycle. Thus successful I2C writes did not establish readable LCD output.
Added explicit E-low startup, 1-second settling, 5-ms initialization gaps,
and conservative 2-ms nibble delays on core 1. Uploaded/verified and received
state=fixed_test_pattern, errors=0 at address 0x27. Temporarily disabled scrolling
(ScrollDemo=false) and wrote exactly:

```
GROUND LCD TEST
0123456789ABCDEF
```

Physical readability remains pending user observation. Do not infer a fixed
hardware or power problem solely from successful backpack ACKs.

## Readability confirmed; scrolling restored

User confirmed GROUND LCD TEST and the numeric/hex row are readable with the
conservative initialization. They report dark/black characters and no apparent
contrast adjustment effect at 3.3V. Optical appearance remains unresolved;
firmware does not change text color. Restored ScrollDemo=true while preserving
all conservative timings. Flash upload verified; scroll frames advance with
zero I2C errors and SD check still passes. See scroll-restored.log.
