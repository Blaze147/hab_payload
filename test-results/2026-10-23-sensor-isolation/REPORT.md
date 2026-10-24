# Sensor / UART payload isolation — 2026-10-23

## Result

The failures follow large host-to-radio UART commands, rather than sensor
polling alone. Pacing the balloon host's UART writes by 250 microseconds per
byte eliminated the observed command timeouts in the controlled 44-byte test.
This is a tested workaround for burst handling, not proof of the exact radio
MCU buffer, electrical, or firmware defect. Neither a general RP2040 compute
limit nor a hard 44-byte LoRa limit is supported by these experiments.

| Experiment | Observation time | UART timeouts | Link outcome |
| --- | --- | --- | --- |
| Original full application, direct USB | 180 s | 46 | 46 link starts, no acquisition |
| GPS reads only, 22-byte GPS envelope | 180 s | 0 | Beacons, handshake, P2 |
| GPS reads only, opaque 44-byte control | USB reports through 36 s | 7 | 8 link starts, no acquisition |
| Full sensor polling, 22-byte GPS envelope | 120 s | 0 | Beacons, handshake, P1 |
| GPS reads only, same opaque 44-byte control, paced UART | 120 s | 0 | Beacons, handshake, P1 |
| Full sensors, paced UART, fixed-duration quiet RF run | 180 s RF; USB replies stopped around 18 s | Host counters unavailable after USB loss | Ground received 65 messages, including 3 real 44-byte envelopes, 6 beacons, and completed handshake |

The opaque control begins `BENCH`, contains a real GPS envelope and filler, and
is deliberately not decoded as genuine additional sensor readings. The final
application restores real sensor telemetry; it does not transmit this test data.
Full sensor activity included camera capture, GPS, BMP280, DHT22, MAX31865 and
DS18B20 polling. The existing intermittent DS18B20/DHT validity failures remain;
invalid sensor records are omitted. Therefore real envelope sizes vary.

## Why beacons disappeared

Most captured UART timeouts were opcode 8 (`SendData`). Each lost UART session
caused reconnect / initialization / link start, restarting the 30-second data
window. Repeated restarts postponed the beacon window. Pacing restored normal
progress into beacon and control windows without changing LinkManager scheduling.
The native split fault reproducer independently demonstrates this mechanism.

## Timing

P0 has a two-second pause **after estimated RF completion**. A 44-byte application
envelope uses 59 RF bytes and approximately 3.81 seconds of airtime. The full
sensor ground capture measured successive full-size P0 intervals of 5818, 5813
and 5807 ms. GPS-only P0 intervals were about 4.76 seconds. Scheduled beacon /
control windows cause legitimate longer gaps; profile upgrades shorten spacing.
This is not a promise of a fixed two-second start-to-start interval.

## Additional USB finding and recovery

The bounded full-load attempt initially stopped in the verbose diagnostic report
at uptime 20009 ms while core 1 continued until watchdog recovery. Saved SRAM
trace: `bounded-trace.bin`, core-0 stage 80, recovery marker 9. This demonstrates
that stalled USB reporting can also stop RF servicing. The report now builds in
a fixed buffer and drains in chunks limited by available USB FIFO space; USB
heartbeat output likewise checks space. The quiet bounded comparison disabled
the verbose report, continued RF through USB loss, and automatically returned to
BOOTSEL at 188.8 seconds. Its ground capture is
`full-paced-bounded-quiet-ground.log`.

The exact USB/controller failure remains unresolved. The normal bench recovery
watchdog still requires processed PC heartbeats and returns to BOOTSEL after
approximately eight seconds without them. The special bounded mode instead
feeds from a progressing main loop until 180 seconds, then expires. A stalled
main core expires within eight seconds in either mode.

## Changes and validation

- `include/BoardSerial.h`: optional UART byte pacing; normal balloon environment
  now selects `BOARD_UART_TX_GAP_US=250`. Baud remains 100000; RF settings unchanged.
- `include/TelemetryMonitor.h`, `include/apps/BalloonApplication.h`, `platformio.ini`:
  reproducible sensor / payload isolation environments; normal sensor logic retained.
- `src/split/host_main.cpp`: buffered diagnostic reports and bounded heartbeat output.
- `tools/balloon-usb-monitor.py`: serial-identity selection, startup enumeration wait,
  heartbeat logging, stops writes when USB replies cease.
- Final normal balloon, ground and paced bench builds passed (`final-builds.log`).
- Split ASan/UBSan tests: 1379 checks passed (`split-tests.log`).
- `git diff --check` passed. Ground firmware was not flashed during these tests.

Artifacts: `balloon-paced-flight.uf2` is the normal build without the PC-dependent
bench guard. `final-bench-paced.elf` includes real sensors, paced UART, buffered
reports, alternate USB stack and the bench recovery guard. Flight readiness and
long-duration reliability have not been established by these short bench runs.

Device identities were checked before loading: balloon `5303284748B66A1C`,
ground `503359276A727B9F`. Current ports were balloon `/dev/cu.usbmodem1101` and
ground `/dev/cu.usbmodem1401`; always rediscover by identity after moving cables.

Final buffered-report bench observation: USB remained responsive through at least
76 seconds, with zero UART timeouts, one link start and an established handshake.
That run later lost USB and its monitor stopped heartbeat writes, triggering
BOOTSEL recovery. This observation is not proof of a permanent USB cure.

The final serialization revision reserves the USB output while a buffered report
drains (`include/UsbOutput.h`); sensor reads continue while their log output waits.
All 11 reports captured in `final-serialized.log` were complete, with zero UART
timeouts. USB stopped replying after approximately 22 seconds; the monitor stopped
writes and the watchdog returned to BOOTSEL. Final hardware state: balloon in ROM
BOOTSEL, available for remote reflashing, not currently transmitting; ground
application and dashboard remain running. The recovery image is
`final-bench-paced.elf`. No continuous USB reliability claim is made.
