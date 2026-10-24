# Ground SD hardware check — 2026-10-22

Ground RP2040 Zero: USB serial 503359276A727B9F, /dev/cu.usbmodem1101.
Detected first as RPI-RP2, loaded split_host_ground UF2, then successfully
uploaded and verified the diagnostic build using PlatformIO over USB.

Wiring configured: SPI1 CS=GP9, CLK=GP10, MOSI=GP11, MISO=GP12.
Mount at 1 MHz failed. A separate low-level probe at 250 kHz also failed:

```
GROUND_SD FAIL stage=mount
GROUND_SD diagnostic_hz=250000 card=0 filesystem=0 error=0x17 data=0x1
```

Installed SdFat error 0x17 is ACMD41 (activate card initialization).
Card initialization did not complete; filesystem and read/write tests could not
run. No file was written and no formatting was attempted. Check adapter power,
common ground, card seating, and the GPIO connections before repeating.

Shared UART connection is not established. LoRa board remains on its preexisting
legacy balloon P0 test firmware; its firmware was not changed in this test.

An existing GPS-map server was temporarily suspended to release the USB serial
port during capture, then resumed afterward.

## Replacement card

User installed another card. Restarted via verified upload of unchanged ground
firmware and captured sd-replacement.log. Same result: mount failure, low-level
250 kHz probe card=0 filesystem=0 error=0x17 data=0x1 (ACMD41 initialization
timeout). No filesystem read/write was reached; no file written or formatting
attempted. The original card alone does not explain the repeated failure.

## 5V adapter supply — PASS

User moved adapter VCC from 3.3V to 5V. Uploaded and verified unchanged ground
firmware, restarting the RP2040 at /dev/cu.usbmodem1201. Captured sd-5v.log:

```
GROUND_SD SPI1 CS=GP9 CLK=GP10 MOSI=GP11 MISO=GP12 hz=1000000
GROUND_SD PASS written=1024 read=1024 verified=1024 removed=1
```

The card mounted; 1024 patterned bytes were written to a new file, closed,
reopened, verified byte-for-byte, and the test file was deleted. No formatting
was performed. This establishes basic SD read/write operation at 1 MHz; it does
not establish sustained logging throughput. Radio UART remains unconnected
with the LoRa board's existing legacy test firmware.
