# Balloon application watchdog and recovery

The normal `split_host` firmware runs independently of this computer. There is
no USB heartbeat requirement, fixed runtime limit, or artificial lockup command.
UART pacing, full sensor polling, and buffered USB diagnostic output remain enabled.

The hardware watchdog is armed before USB initialization and core-1 startup.
Only completion of a main-loop iteration feeds it. If startup or the main loop
stops progressing for approximately eight seconds, the RP2040 resets and starts
normal operation again, including both application cores and sensor startup.
Watchdog resets never automatically enter BOOTSEL, regardless of USB connection.
The reset reason remains available in the diagnostic trace (trace[8] is one for
an application-watchdog recovery). There is only one hardware watchdog.

This timeout measures a real lack of loop progress; it is not an eight-second
runtime limit. Closing a serial monitor, unplugging USB while battery-powered,
or running without a computer does not by itself trigger a reset. If only USB
stops responding while the application loop continues, this watchdog does not
force a reset: RF can continue, although USB access may remain unavailable.
It does not independently supervise a core-1-only stall unless that also blocks
the main loop. The underlying USB failure is still unresolved. A persistent
startup fault can cause repeated resets; this policy does not yet provide a
reduced-function recovery application.

## Deliberate USB firmware recovery

There is no custom handshake, background recovery program, computer heartbeat,
or startup USB detection window. macOS enumerates the USB device using its
normal USB/serial drivers. Enumeration alone does not request BOOTSEL.

The installed Arduino/TinyUSB stack already implements the standard 1200-baud
serial touch: set the first USB CDC port (Serial) to 1200 baud, then deassert
DTR (normally by closing the port). Its `tud_cdc_line_state_cb` calls
`TinyUSB_Port_EnterDFU`, which calls `reset_usb_boot(0, 0)` on RP2040.
A deliberate upload/recovery action can use these ordinary serial controls;
ChatGPT does not need to be running or listening. Normal monitors should use
115200 baud. BOOTSEL entry deliberately stops telemetry until reboot/upload.
On the normal balloon build, this explicit USB bootloader request sets the
RP2040 Zero LED solid purple before entering ROM BOOTSEL. The WS2812 retains
that colour while ROM runs, distinguishing requested BOOTSEL from red P0 packet
activity. This does not apply to physical BOOTSEL entry, which bypasses the
application. Watchdog resets still restart the normal application.
The balloon linker wraps `TinyUSB_Port_EnterDFU()` to set the colour and wait
400 microseconds for the PIO transmission/latch; installed framework sources
are unchanged. `USB_BOOTSEL_PURPLE` and its paired linker flag enable this hook.

USB recovery requires a working enough USB stack to handle that request.
A frozen application first gets the normal watchdog restart, after which USB
recovery can be requested again. A persistent failure before USB becomes usable
may still require physical BOOTSEL access. Ground firmware is unchanged and
does not enable the balloon watchdog.

## Troubleshooting: USB unavailable or upload cannot enter BOOTSEL

The 1200-baud touch is handled by the application USB stack. If that stack is
unresponsive, the computer cannot use this mechanism to request BOOTSEL. This
does not mean the entire application has stopped: radio telemetry may still be
running, and a USB-only failure does not expire the application watchdog.

When diagnosing this problem:

1. Check ground reception to distinguish lost USB access from lost telemetry.
2. If the main loop has also frozen, allow approximately eight seconds for the
   hardware watchdog to reset the RP2040, then allow normal startup and USB
   enumeration before retrying the upload. Rediscover the balloon by its serial
   identity below; the previous port name may no longer be valid.
3. If the application repeatedly fails before USB becomes usable, software USB
   recovery may remain unavailable. Physical BOOTSEL entry is the fallback when
   the board is accessible. Do not restore automatic BOOTSEL-on-watchdog-reset
   as a workaround: it would leave an airborne payload waiting for an upload.

**Flight without USB:** no computer, serial monitor, handshake, or USB connection
is required for operation or watchdog recovery. The watchdog resets the chip
independently of USB, and normal startup resumes both application cores and
sensors. Missing USB alone causes neither a reset nor BOOTSEL entry. A persistent
underlying fault can still produce repeated resets without restoring telemetry;
automatic restart is a recovery attempt, not a guarantee that the fault clears.

With the updated restart-flag firmware on both endpoints, the ground web UI
colors a packet dark red when it carries the sender-restarted flag; its tooltip
says “Sender restarted (RP2040 or radio)”. Either MCU restarting, including a
normal power-on, arms the same flag. It does not identify the cause or which MCU
reset. Only the first eligible transmission is marked: data, or an otherwise
unflagged control packet. Beacons and SYN/SYN-ACK/ACK packets carry no restart
indication and leave it pending. Missing the marked packet misses the indication. With older LoRa firmware, `restartFlagSupported=0` means
telemetry remains compatible but restart packet flags are not yet available.
See [the flag semantics](ProtocolManager.md#sender-restarted-flag).

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
do not deploy them. The historical BOOTSEL-on-timeout deployment and validation are recorded in
`test-results/2026-10-23-standalone-watchdog/REPORT.md`; that image predates the
restart-on-timeout policy above and must not be used as a flight recovery image.
