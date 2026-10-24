# Purple indication for watchdog BOOTSEL recovery

The unresolved USB loss is separate from the deliberate recovery policy. Latest
port-recheck trace showed USB SOF stopped advancing while both cores progressed
until watchdog reset. The bench heartbeat watchdog then stopped the application
by entering ROM BOOTSEL; it does not automatically resume telemetry.

Added a recovery-only callback to `usb_recovery_diagnostic::begin()`. The host
passes `status_led::bootloaderRecovery`, which initializes the RGB LED, sends
RGB (40, 0, 40), and waits 400 microseconds for the PIO frame / LED latch before
entering ROM. It runs before USB initialization and core-1 sensor startup.
Manual BOOTSEL entry does not execute this application callback. Regular startup
retains the existing white startup and packet colours.

Physical forced-stall validation: deliberately disabled core-0 interrupts and
looped forever. Device returned to ROM BOOTSEL in 8.498 seconds without buttons.
Saved SRAM confirmed recovery stage 9, forced HALT marker, and trace[8]=1 indicating
the purple callback completed. This verifies the recovery path and issued LED
update; no camera/visual inspection of the physical LED was available.

Both `split_host` and `split_host_uart_paced` builds passed. `git diff --check`
passed. Archived `balloon-purple.elf` and `.uf2` are the new full-sensor paced
bench firmware with the heartbeat watchdog. Canonical sensor-isolation
`final-bench-paced.elf` was updated; its prior image is preserved as
`final-bench-before-purple.elf` for historical trace interpretation.

Final hardware state: balloon flash identity 5303284748B66A1C, in BOOTSEL after
the deliberate test with purple indication issued; no telemetry currently sent.
Ground station was not changed. Restarting this bench firmware still requires
the PC heartbeat monitor. The exact USB failure remains unresolved.
