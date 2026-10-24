# Standalone balloon watchdog — 2026-10-23

## Policy change

The normal `split_host` image now uses an application-progress watchdog. It has
no computer heartbeat requirement, forced runtime deadline, or artificial crash
command. Completing a main-loop iteration feeds the hardware watchdog. A genuine
startup / main-loop stall lasting approximately eight seconds causes recovery.
After that reset, the early hook issues solid purple and enters ROM BOOTSEL before
USB initialization or core-1 sensor startup. BOOTSEL waits for reboot / upload;
it does not resume telemetry automatically.

The normal full-sensor, UART-paced application is installed, using the same
alternate USB stack as the preceding paced bench firmware. Removing the
computer connection / serial monitor cannot itself expire the watchdog while
the main loop continues. A USB-only failure with a progressing application does
not trigger recovery; RF may continue while USB remains unavailable. Core-1-only
stalls are not separately supervised unless they block the main loop.

## Removed active test behavior

- Deleted the `h` artificial interrupt-disable / infinite-loop handler.
- Deleted `k` heartbeat handling and watchdog feeding from PC commands.
- Removed fixed-duration watchdog feed / shutdown mode.
- Removed the USB-only stress probe source and its configuration environments.
- Renamed the watchdog header / namespace to `ApplicationWatchdog.h` /
  `application_watchdog` and enabled it in the normal balloon environment.
- Converted the optional USB logger to read-only; it sends no command bytes.

Historical scripts / images under `test-results/` are retained as evidence of
retired experiments and must not be deployed. `docs/USB_RECOVERY.md` now describes
the standalone policy and identifies the current image here. The purple callback
is unchanged from the preceding physically verified recovery test; no artificial
freeze was injected for this deployment.

## Validation

Normal balloon and ground builds passed (`build.log`); ground was not reflashed.
`git diff --check` and Python logger syntax validation passed. Source checks found
no artificial HALT marker, heartbeat handler, or fixed recovery deadline in active
source/configuration. The watchdog feed is only at completion of the main loop.

Installed `balloon.elf` using picotool verified load after confirming balloon
flash identity `5303284748B66A1C`. Ground identity `503359276A727B9F` stayed running.
The physical observation sends zero heartbeat and zero fault commands. It reads
startup USB output for 15 seconds, closes that serial port, and continues to
observe RF through the existing ground dashboard. See `observe.py`,
`observe-console.log`, `result.json`, `startup.log` and `ground.log` for the result.

The new firmware is archived as `balloon.elf` and `balloon.uf2`. The standard
future build command is `pio run -e split_host`.

The old sensor-isolation `final-bench-paced.elf` deployment name was retired
as `retired-pc-heartbeat-purple.elf` to prevent accidental reuse of the previous
PC-dependent image. Use the current normal build / image in this directory.

Completed physical observation: PASS at 180 seconds. No heartbeat / fault
commands were sent; USB capture closed at 15 seconds. BOOTSEL checks at 60,
120 and 180 seconds were all negative. Ground received 53 messages and
6 beacons, completed handshake=True, and received data in
profiles P0, P1, P2. Payload sizes/counts: {'22': 1, '44': 15, '40': 27, '32': 10}.
Final state: balloon application remains running independently with full sensors;
no balloon USB monitor remains attached. Ground dashboard continues running.
