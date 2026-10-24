# Higher-rate UART validation — 2026-10-22

The board-to-board UART was increased from 38400 to **100000 baud, 8N1**, and
both connected boards were reflashed with production firmware at that rate.
USB serial diagnostics remain at 115200; they are independent of Serial1.

## 115200 attempt

The standard 115200 setting was first built and flashed on both boards. Ideal
byte-paced software simulation passed all 335 checks, but the physical UART
could not establish a session. The radio reported zero accepted commands and
increasing malformed-frame counts. The diagnostic run failed 167 checks and
received no messages. These failures are retained in
[the 115200 log](../2026-10-22-uart-115200/selftest-1.log).

The installed Arduino AVR core selects U2X mode with UBRR=8 at 8 MHz for this
setting, yielding 8000000 / (8 * (8 + 1)) = 111111.11 baud, approximately 3.55%
below 115200. The RP2040's configured rate differs enough to cause errors on
this hardware. The native simulator models byte transport and buffering, not
UART bit sampling or oscillator tolerance, so it cannot detect that failure.

## 100000 result

100000 is a custom rate in the requested range. The AVR generates it exactly
with U2X and UBRR=9; both boards now use the same configured rate. This provides
approximately 2.60 times the previous throughput. A raw 255-byte transfer takes
25.5 ms before framing, metadata and escaping, versus about 66 ms at 38400.

- All **335 split-system simulation checks passed** under ASan/UBSan.
  Simulator byte duration now derives from the shared Baud constant, rather than
  remaining hard-coded at the old 38400 timing.
- All five production/diagnostic/ground firmware targets built successfully.
- **Three consecutive physical diagnostic runs passed 170 checks each: 510 total.**
- 96 maximum-size synthetic packet decoder/UART/application round trips delivered
  all 240 payload bytes correctly, with zero invalid messages.
- 30 actual local RF transmissions completed and 300 receive commands succeeded.
- Corrupt/malformed protocol observations and deadline rejection were checked.
- Both sides reported zero UART frame errors; the host reported zero timeouts,
  event gaps, queue overflows and radio errors across all three runs.

See [simulation](simulation.log), [builds](builds.log),
[run 1](selftest-1.log), [run 2](selftest-2.log), [run 3](selftest-3.log), and the
production upload logs in this directory.

The physical devices remain BSFrance /dev/cu.usbmodem1301 and RP2040
/dev/cu.usbmodem1101. Production balloon scheduling was restored after diagnostics.
The packet receptions above were injected on the radio board, not received over
RF; two-endpoint RF validation still requires a second endpoint.

A final **60-second production UART observation passed with zero failures**.
Host radio errors, timeouts, UART errors, event gaps and overflows remained zero,
and the radio reported zero malformed frames and rejected commands. See
[production observation](production-soak.log). Both boards are left running
production firmware at 100000 baud.
