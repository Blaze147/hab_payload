# Balloon application watchdog and recovery

The normal `split_host` firmware runs independently of this computer. There is
no USB heartbeat requirement, fixed runtime limit, or artificial lockup command.
UART pacing, full sensor polling, and buffered USB diagnostic output remain enabled.

The hardware watchdog is armed before USB initialization and core-1 startup.
Only completion of a main-loop iteration feeds it. If startup or the main loop
stops progressing for approximately eight seconds, the RP2040 resets. The early
recovery hook sets the RGB LED solid purple and enters ROM BOOTSEL before USB
application or sensor initialization can interfere. Purple means the application
is stopped and available for recovery, not that a packet is transmitting.

This timeout measures a real lack of loop progress; it is not an eight-second
runtime limit. Closing a serial monitor, unplugging USB while battery-powered,
or running without a computer does not by itself trigger recovery. If only USB
stops responding while the application loop continues, this watchdog does not
force a reset: RF can continue, although USB access may remain unavailable.
It does not independently supervise a core-1-only stall unless that also blocks
the main loop. The underlying USB failure is still unresolved.

Watchdog recovery deliberately waits in BOOTSEL until rebooted or reflashed;
it does not automatically resume telemetry. Manual BOOTSEL entry bypasses the
application's purple-indicator hook. A normal restart restores normal startup
and packet colours. Ground firmware is unchanged and does not enable this
balloon watchdog.

| Endpoint | Stable application USB serial / flash identity |
| --- | --- |
| Balloon | `5303284748B66A1C` |
| Ground | `503359276A727B9F` |

Build the standard balloon image with:

```sh
~/.platformio/penv/bin/pio run -e split_host
```

Before flashing, rediscover devices by identity rather than trusting a previous
port. With the intended balloon verified in BOOTSEL, use picotool `load -v -x`
with the built ELF. No monitor is required afterward. The optional read-only
logger is:

```sh
~/.platformio/penv/bin/python -u tools/balloon-usb-monitor.py \
  --log test-results/balloon-usb-current.log
```

The former PC-heartbeat watchdog, `h` artificial-lockup command, USB probe
program, and fixed-duration recovery configuration were removed. Historical
images and test scripts under `test-results/` describe those retired experiments;
do not deploy them. The current image and validation are recorded in
`test-results/2026-10-23-standalone-watchdog/REPORT.md`.
