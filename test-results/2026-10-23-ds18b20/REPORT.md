# Internal probe integration and deployment (current pin GP27)

**The full balloon firmware is flashed and running. The new probe has not
returned a valid temperature; it repeatedly reports NO_DEVICE. Further physical
power/wiring/probe identification is required.**

## Implemented

- DS18B20 on GP27 (initial test used GP26), OneWire 2.3.8 / DallasTemperature 3.11.0.
- Async 12-bit conversion, 800 ms wait, roughly one request per second.
- ROM family 0x28/CRC validation, scratchpad CRC handled by library, -55..125 C
  limits, no EEPROM auto-save. Retry discovery every five seconds.
- MessageManager ID 3 now identifies the internal low-temperature DS18B20,
  replacing the earlier inaccurate InternalRtd reservation. Desktop field:
  sensors.internal_low_temperature_c.
- External MAX31865/PT100 remains ID 4 / sensors.external_rtd_c with the same
  -0.89 C correction. DHT temperature/humidity remain distinct.
- Unknown, invalid or stale probe samples are omitted from telemetry.
- A valid combined GPS/DHT/both-probe snapshot is 38 bytes.
- Ground decoder source updated and built, not flashed (ground USB unavailable).
  Existing radio coprocessor not reflashed: these sensor messages fit its old
  single-packet limit; large messages still require radio firmware updates.

## Hardware observations

RP2040 Zero serial 5303284748B66A1C, /dev/cu.usbmodem1201.

Full-firmware upload/flash verification passed. A1 MessageManager telemetry was
accepted by the local radio (packet_result=0), with external_rtd_valid=1 and
internal_probe_valid=0. This does not establish ground RF receipt.

The new probe repeatedly reports devices=0, idle_high=1, presence=0. The user
confirmed GP26 and a signal-to-VCC pull-up. Requested probe wire colors, terminal
mapping, resistor value and explicit 3.3 V/shared-ground confirmation.

A corrected minimal standalone test reproduces the missing response without
GPS/camera/DHT/application activity: OneWire library_presence=0, devices=0;
independent direct-GPIO reset followed by 5 us waveform sampling finds no low
presence pulse (sampled_low_slots=0). Probe model is not electrically confirmed.

Final full-firmware runtime confirms the host is connected to its radio, normal
USB sensor reports have resumed, and the new probe remains absent. Final PT100
range: 23.63..23.69 C, all fault=0x00 and offset_c=-0.89.
GPS fix/data and valid DHT readings were observed. DHT also intermittently
reports READ_ERROR, which remains invalid and is omitted as appropriate.
Two camera frames in initial-runtime.log passed length, CRC32 and JPEG marker
checks; recovered images are camera-1.jpg and camera-2.jpg.

## Diagnostic incident and recovery

The first standalone test incorrectly used Arduino delayMicroseconds (sleep_us)
inside an interrupt-disabled section, freezing USB. User entered BOOT mode;
full balloon firmware was restored through the UF2 volume. The diagnostic was
fixed to use busy_wait_us_32 and an eight-second watchdog. The corrected test
completed normally. A final ordinary full-firmware upload succeeded afterwards;
the board is not left in diagnostic mode. upload.log records this final success.

## Software verification

- Balloon and ground release builds pass.
- Native ASan/UBSan: 17270647 checks, including new internal-probe limits and
  typed identity, plus fragmentation/reassembly/32 KiB message tests.
- Split ASan/UBSan: 1375 checks including 1000-byte RF/UART message transfer.
- Python: 31 tests pass.
- git diff --check passes.

Recovery image: balloon-firmware.uf2
SHA256: 4d09464ad38fd22ff1732fd1a446d19b822b6d5d21dee1770fe4683d2834a0a3

No valid DS18B20 hardware temperature or cold calibration was obtained.

## GP27 follow-up

User moved the signal to GP27. DataPin and documentation were updated, and
balloon-gp27-firmware.uf2 was built. Automatic 1200-baud upload failed to enter
BOOTSEL. User re-entered BOOT mode; the UF2 was copied to RPI-RP2, the board
rebooted, and gp27-runtime.log confirms the current full firmware reports
DS18B20 pin=27 and MessageManager schema=A1.

The probe remains NO_DEVICE, devices=0, idle_high=1, presence=0 on GP27.
Telemetry correctly has internal_probe_valid=0 and external_rtd_valid=1; local
radio packet_result=0 is observed. Requested wire-color/terminal mapping,
resistor value and explicit 3.3 V/shared-ground confirmation to continue.
The terminal adapter marking does not by itself establish probe model.

Current recovery image: balloon-gp27-firmware.uf2
SHA256: e4b7b9c4dacace75aae239f0528b6491aa76949a97b1f2df95944d5708a1b795
