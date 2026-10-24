# Balloon port and solid-red LED check

Identified ground application serial `503359276A727B9F` at
`/dev/cu.usbmodem1401`, USB location `0-1.4`. The balloon initially had no
application serial port. Picotool identified its flash ID `5303284748B66A1C`
in ROM BOOTSEL, and IORegistry placed RP2 Boot at hub location `0-1.2`.

Saved SRAM recovery trace before changing the device: `recovery-trace.bin` and
`recovery-trace.json`. It contains the armed marker, watchdog reason 1 and
recovery stage 9. Both application cores last progressed at approximately
8002 ms; USB SOF changes were recorded through 8002 ms. No balloon heartbeat
monitor was running. This supports the bench firmware expiring its eight-second
heartbeat watchdog after reconnect/startup, rather than a P0 RF transmission
remaining active for minutes. It does not establish a USB controller fault in
this particular run.

The WS2812 retains its last programmed colour across a processor reset until
sent another update. ROM BOOTSEL does not run `status_led::poll()`, so a red LED
can remain latched while the application is stopped. The LED is not independent
confirmation that the radio is transmitting.

Restarted the existing firmware with `picotool reboot` (no firmware flash or
source build), then started the serial-identity-selected heartbeat monitor.
Balloon application reappeared at `/dev/cu.usbmodem1201`, USB location `0-1.2`.
Updated its PlatformIO upload-port hint; stable-identity monitor selection remains.
Ground was not reset or reflashed.

Ground confirmed new P0 messages from session 35652, initially 22 then 40 bytes.
Initial balloon reports show one link start, zero UART timeouts/errors and USB
heartbeat responses. Live log: `balloon.log`; monitor PID in `monitor.pid`.
The diagnostic image still requires PC heartbeats; loss of the monitor returns
it to BOOTSEL rather than automatically resuming RF transmission.

Subsequent observation: ground received 7 data messages and six beacons.
USB replies stopped again while the monitor was running. The monitor stopped
heartbeats and picotool confirmed watchdog recovery to BOOTSEL again.
Final state: balloon in BOOTSEL, no current telemetry transmission.
Final SRAM trace: recovery stage 9, core 0 stage 24 at 46418 ms,
core 1 at 46417 ms, last observed USB SOF change at 38868 ms.
