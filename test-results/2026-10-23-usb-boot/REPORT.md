# USB recovery investigation — 2026-10-23

Balloon USB serial: `5303284748B66A1C`. Ground: `503359276A727B9F`.
The original 2 MB flash was saved as `original-flash.bin` before experiments.

The full application intermittently stopped answering USB while its main loop
and sensor core continued running. This happened through the shared hub and
through a direct computer connection. Neither an exclusive USB mutex owner nor
a globally disabled core interrupt state explained the captured failure.
The exact USB failure trigger remains unresolved. USB-only, multicore USB,
serial reopen, and deliberate read-backpressure controls passed their bounded
runs. The alternate USB stack passed a three-minute direct-port run, but that
is not evidence of a permanent cure.

A separate **bench-only** recovery guard starts before USB/core 1 initialization.
It requires `k` USB heartbeats and returns to ROM BOOTSEL after an eight-second
watchdog expiry. Recovery executes before application/sensor startup. A physical
fault test disabled core 0 interrupts and entered an infinite loop; BOOTSEL
returned without buttons in 8.316 seconds (`verify_hard_stall` artifacts).
Normal flight builds do not enable this PC-dependent guard. See
`docs/USB_RECOVERY.md` for operating instructions.

The three-minute full application run (`direct-tinyusb.log`) retained USB
communication but accumulated 46 UART command timeouts and 46 link starts.
USB and radio UART failures therefore need separate diagnosis. The user
accepted deferring the exact USB cause and requested sensor/payload isolation;
those experiments are in `../2026-10-23-sensor-isolation/`.
