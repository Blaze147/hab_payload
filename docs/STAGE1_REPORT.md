# Stage 1 development report — 2026-10-09

Implemented the reusable Arduino-independent RadioManager, LoRa hardware adapter,
permanent deterministic simulator/channel, native tests, and one-way and exchange
firmware applications. Production contains no simulator or allocation-based queue.
No later managers, production scheduling, negotiation or retransmission were added.

## Executed successfully on the development machine

- `pio test -e native`: six test groups passed (initialization/configuration,
  packet boundaries/binary transfer, bidirectional operation, profile changes,
  errors/timing, repeated operation).
- `./tools/test-native.sh`: 262,574 explicit checks passed, including 1,000
  alternating full 255-byte transfers, with AddressSanitizer and UBSan enabled.
  No sanitizer findings. Compiler warnings are errors for this host run.
- `pio run -e transmitter -e receiver -e transmitter_test -e receiver_test -e transmitter_bidirectional -e receiver_bidirectional`:
  all six firmware builds succeeded without attached hardware.

| Environment | Static SRAM / 2560 bytes | Flash / 28672 bytes |
| --- | ---: | ---: |
| transmitter | 497 (19.4%) | 9688 (33.8%) |
| receiver | 497 (19.4%) | 9688 (33.8%) |
| transmitter_test | 942 (36.8%) | 12558 (43.8%) |
| receiver_test | 909 (35.5%) | 11330 (39.5%) |
| transmitter_bidirectional | 950 (37.1%) | 12686 (44.2%) |
| receiver_bidirectional | 928 (36.2%) | 12486 (43.5%) |

Static SRAM figures do not include peak stack usage. Embedded core/adapter uses
no dynamic allocation. Toolchain: PlatformIO Atmel AVR 5.3.0, Arduino AVR 5.4.0,
AVR GCC 7.3.0. Native platform 1.2.1, Unity 2.6.1, local Apple C++ compiler.
AVR framework produces existing USB LED macro warnings (`statement has no effect`);
these are in framework code, not test failures.

## Configuration preservation and deliberate changes

Board `lora32u4II`, Arduino framework, CS8/reset4/DIO0=7, 915 MHz, 115200 serial,
existing transmitter/receiver names and COM5/COM4 defaults are preserved. The
original main is retained at `docs/reference/original-main.cpp.txt`.
Shared board options moved from `[env]` to `[radio_board]` with explicit inheritance
so the native build cannot inherit AVR framework/board settings. This was necessary:
empty native overrides of inherited framework/board caused BoardConfig errors.
LoRa pinned to inspected version 0.8.0 for the documented register polling bridge.
P0 settings are now explicit (SF12/BW125k/CR4/8/17dBm/CRC); original modulation
was implicit library defaults. P1 is manually selectable SF9/CR4/5. Test interval
is now nonblocking and measured from completion rather than a blocking delay.

## Pending hardware and operating validation

No uploads or RF communication tests were performed. Physical SPI wiring,
reset, RF variant, antenna behavior, receive rearming, IRQ polling bridge, RSSI/SNR,
maximum payload reception, real airtime and failure recovery remain unverified.
Perform the 10-packet/1,000-packet/two-profile/exchange/restart procedure in
HARDWARE_TESTING.md. P0's relative reliability is provisional pending measurement.
US operating authorization and permissible settings must be established before
RF emission; example profiles are not a certification/compliance claim.

## Functional limits

Simulator airtime is approximate; there is no collision/capture, propagation or
link-budget model. Driver configuration setters do not confirm register writes;
some faults cannot be detected. Hardware receive buffering is a single FIFO and
has service/rearm gaps. Native tests verify the real core through substitute
hardware, not SPI or the hardware adapter. Zero-length RX behavior is documented
but actual empty/CRC-error packet handling remains a hardware check. Automatic
recovery is application policy and retries initialization every five seconds.
Physical full validation remains pending even though development builds/tests pass.

## Optional hardware test application

At the user's request, test behavior moved from main.cpp into
src/hardware_test/HardwareTestApp.cpp. Default transmitter/receiver environments
exclude that source and initialize into idle. Explicit transmitter_test,
receiver_test and bidirectional environments enable it. All six embedded builds
and six native test groups passed after this separation. Default ELF symbol
inspection found no hardware_test symbols, test buffer or counter. Default builds
use 497 bytes static SRAM; no test packets are sent. Recovery retries described
above apply to the optional test application only.
