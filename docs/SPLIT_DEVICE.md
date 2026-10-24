# Split radio device

The default build now produces two firmwares for **one** endpoint. A second
endpoint is still required for an over-the-air link conversation.

| Board | Runtime ownership |
| --- | --- |
| BSFrance LoRa32U4 II | PhysicalRadioAdapter, RadioManager, ProtocolManager, UART server |
| Waveshare RP2040 Zero | StatisticsManager, LinkManager, MessageManager, application and UART client |

The UART client is a PacketService implementation; it does not instantiate a
ProtocolManager or radio hardware driver on the RP2040. Shared packet schemas,
structural validators, modulation definitions and the airtime formula are compiled
where needed. StatisticsEvents defines a small observation sink without requiring
StatisticsManager on the AVR. Existing single-board environments remain available.

## Wiring and USB

| RP2040 | BSFrance |
| --- | --- |
| GP0 / UART0 TX | D0 / RX1 |
| GP1 / UART0 RX | D1 / TX1 |
| GND | GND |

The user's 0-to-0 and 1-to-1 connections therefore already cross TX/RX correctly.
Use compatible 3.3 V logic and short wiring. USB powers/programs each board;
`Serial` is USB diagnostics and `Serial1` is the board-to-board connection.
The firmware does not require a computer or an open USB serial monitor to run.

Either MCU restarting arms a sender-restarted flag on the next eligible LoRa transmission
(data, or control with no existing flags). Beacons and SYN/SYN-ACK/ACK stay
unchanged and leave the indication pending.
The LoRa ProtocolManager arms it at construction; the RP2040 RemoteProtocol sends
a `MarkRestart` UART command once after its own construction, before allowing
normal operation. A UART reconnection alone does not announce a reboot. See the
[packet flag definition and delivery limits](ProtocolManager.md#sender-restarted-flag).
The full extension needs updated LoRa and RP2040 images on both endpoints.
RP2040-only updates remain compatible with the older radio firmware: the host
accepts legacy UART status replies and omits the unsupported restart command.
Telemetry continues, but restart flagging waits for the LoRa firmware update.
The USB status field `restartFlagSupported=0` reports this mode.

UART uses **100000 baud, 8N1** on both sides. This is an exact divisor of the
8 MHz AVR clock and about 2.6 times the original 38400 setting. The standard
115200 setting was tried on both physical boards but failed to establish a
session: the AVR actually generates about 111111 baud at that setting, while the
RP2040 generates approximately the requested rate. The 100000 setting avoids
that mismatch. A 255-byte raw transfer alone takes 25.5 ms; framing, metadata,
and escaping add time. Events and commands must be serviced frequently; do not
add blocking application work to either loop. This does not establish maximum
sustainable P4 packet rate.

USB port names can change. Identify the BSFrance by USB VID:PID 239A:800C;
the running RP2040 reports RP2040 Zero, VID:PID 2E8A:0003 and a device serial.
The RP2040 boot ROM instead mounts RPI-RP2. The hardware test tool checks both
USB identity and the running RP2040 description rather than assuming port order.

## Build and upload

```sh
pio run                         # split_radio + split_host (balloon)
pio run -e split_host_ground     # alternative RP2040 ground policy
./tools/test-native.sh
./tools/test-split.sh

pio run -e split_radio -t upload --upload-port <BSFrance-port>
pio run -e split_host -t upload --upload-port <RP2040-port>
```

If the RP2040 has no running USB firmware, hold BOOT while connecting it and copy
`.pio/build/split_host/firmware.uf2` to RPI-RP2. On macOS, `cp -X` avoids attempts
to copy extended attributes to that boot drive. Subsequent uploads use picotool
and the running USB device. The RP2040 platform is pinned to a specific commit.

The default RP2040 firmware is **balloon** policy. Use `split_host_ground` for a
ground endpoint; the radio firmware is identical for either role. USB commands:
`s` prints status, `x` stops link scheduling and closes the data gate, and `r`
restarts scheduling. An RF transmission already started is allowed to finish.

## Ground SD card check

The `split_host_ground` application configures the HW-125 microSD adapter on
SPI1: CS GP9, CLK GP10, MOSI GP11, MISO GP12 (GPIO labels, not physical pin
positions). GP0/GP1 remain the radio UART. The adapter also needs appropriate
power and common ground. The ground station's HW-125 is powered from the
RP2040 Zero's 5V pin, with common GND and 3.3V GPIO signaling. On this hardware,
3.3V into the adapter's VCC caused card initialization timeouts; switching VCC
to 5V passed the 1024-byte write/read/verify/delete check. See
`test-results/2026-10-22-ground-sd/sd-5v.log`.

On boot, core 1 mounts the card at 1 MHz with automatic formatting disabled,
chooses an unused `/GS000000.TST` through `/GS000999.TST` filename, writes
1024 patterned bytes, closes and reopens the file, verifies its size and every
byte, and removes only that test file after successful verification. A failed
test may leave its newly created file for inspection. Core 0 reports
`GROUND_SD PASS written=1024 read=1024 verified=1024 removed=1` or a failure
stage over USB at 115200 baud, repeated every 10 seconds for monitors attached
after boot. A mount failure also probes card and filesystem initialization at
250 kHz and reports the SdFat error code. Reset the RP2040 to run the test again.

Build with `pio run -e split_host_ground`; upload only to the identified ground
RP2040, or copy `.pio/build/split_host_ground/firmware.uf2` to its RPI-RP2 boot
drive. A successful build alone does not verify the physical SD card.

## Ground LCD telemetry display

The ground application drives the SunFounder LCM1602/PCF8574 16×2 LCD on
Wire1/I2C1 at 100 kHz: SDA GP2, SCL GP3, common GND. Core 1 owns all LCD
I/O and retains the verified initialization and conservative nibble delays.
It initializes only when exactly one PCF8574 address acknowledges. The user
resolved the earlier brightness issue and confirmed readable characters.

The fixed top row shows the negotiated data profile (`P?` without an established link), latest received RSSI
in dBm (`R`), and latest received SNR in dB (`S`, one decimal place). Example:
`P0 R -52 S 11.8`. Fields use one space between items; if the text exceeds
16 characters, spaces after `R` and `S` are removed. The top row never scrolls.
Signal fields show `--` until a reception measurement exists.
The last signal measurements remain displayed between packets.

The bottom row moves leftward every 350 ms through latitude, comma,
longitude (three decimal places), then `-<age>S`, e.g.
`39.564,-105.017 -120S`. A valid decoded GPS position updates both coordinates
and their receive timestamp. Invalid fixes, other packets and missing radio
traffic do not clear the position or reset its age. Until the first fix, the
row says `Waiting for GPS`; no position is fabricated. Age is elapsed time
since receiving the last valid coordinates, not the balloon GPS fix age.
Position retention lasts for the current ground-host boot.

The scrolling text is a snapshot: both coordinates and their age are frozen
for one complete pass. At the start of the next pass, it samples the newest
valid position and its current age. Top-row signal values continue updating
independently. LCD numbers use integer formatting, and every row is padded
to 16 printable ASCII characters.

A short mutex-protected snapshot carries telemetry from core 0 to core 1;
LCD writes run outside the lock. The radio callbacks do no I2C work and LCD
updates do not depend on USB being connected or its output queue having room.
USB reports the detected address, state, frame count and write errors every
five seconds. Visual appearance still needs observation on the physical LCD.

## MAX-M10S GPS application

Balloon `split_host` variants use `include/GpsMonitor.h` and TinyGPSPlus 1.1.0
with the MAX-M10S's default NMEA stream over Qwiic/I²C at address `0x42`.

| MAX-M10S Qwiic | RP2040 Zero GPIO |
| --- | --- |
| SDA (blue) | GP2 (I2C1 SDA) |
| SCL (yellow) | GP3 (I2C1 SCL) |
| Power (red) | 3.3V |
| GND (black) | GND |

GPIO numbers refer to GP labels, not physical header positions. GPS on core 0 and camera on core 1 share `Wire1` (I2C1), GP2/GP3,
at 100 kHz. `BalloonI2c.h` serializes initialization and complete transactions
with a cross-core mutex. GPS address is `0x42`; camera address is `0x30`.
The GT-U7 UART initialization and GP6 PPS interrupt are removed. GP8 remains
assigned to the DHT22. No separate PPS wire is required or currently sampled.

At 115200 baud the RP2040 USB monitor emits one `GPS` line per second with
latitude/longitude, altitude (meters), speed (km/h), satellites, UTC date/time,
character/checksum counts, and cumulative `i2c_errors`. `sensor=MAX-M10S`
identifies the receiver; `pps=NA pps_age_ms=NA` indicates unavailable PPS.
`NO_DATA` means no incoming bytes in three seconds; `NO_FIX` means no fresh
valid position. Position, altitude, speed, satellites and UTC expire after three
seconds, and unavailable fields are reported as `NA` (satellites as `-1`).

The helper polls every 50 ms at 100 kHz, consuming at most 256 bytes in 32-byte
chunks per poll, with a 10 ms I²C transaction timeout. It retries automatically
on subsequent polls after bus errors and never waits for a fix or USB connection.
USB reports are dropped under backpressure. Receiver configuration is unchanged.

The current source uses MessageManager A1 typed telemetry: GPS, internal DHT22
temperature/humidity and corrected external MAX31865 temperature. Missing or
stale sensors are omitted. Speed and UTC remain USB-only. See
[MessageManager.md](MessageManager.md) for the codec and deployment boundary;
the Python ground bridge also retains HABT-v1 backward compatibility.

## DHT22 temperature and humidity

`include/EnvironmentMonitor.h` reads the sensor with DATA on **GP8**. Although
initially described as a DHT11, the unmarked device was verified to emit DHT22
16-bit tenths encoding (e.g. bytes `01 99 01 0B A6` = 40.9% RH, 26.7 °C),
so firmware uses DHT22 decoding. Use a shared
GND and 3.3 V compatible data levels. A bare sensor needs a data pull-up resistor
to 3.3 V; many breakout modules already include one.

The Adafruit DHT library runs on core 1, including its timing-sensitive interrupt
lock and two-second startup wait. Core 0 continues servicing GPS, PPS, USB, and
the radio UART. A mutex protects only the short shared reading copy; the main
loop never waits for the sensor. Core 1 has a separate stack.

A checksum-validated sample is taken about every 2.1 seconds and emitted as
`ENV OK c=... humidity_pct=... sensor=DHT22 pin=8 sequence=...`. Failed reads
are `ENV READ_ERROR c=NA humidity_pct=NA`, never substituted with old readings.
Temperature must be within −40–80 °C and humidity within 0–100% for acceptance.
This is a functional check, not a calibration of the sensor's accuracy.

The local map bridge exposes `environment` and `environment_age_seconds` on
`/api/gps`, independently of GPS freshness. USB reports retry if the buffer is
full, retaining only the most recent reading. No sensor data is sent over LoRa.
The former SPI temperature helper and dependency have been removed; GP9–12 are
no longer configured by this application.

## Arducam snapshot application

The UC-C78-marked board was identified electrically as an **OV2640** (sensor ID
`0x2641`). `include/CameraMonitor.h` uses hardware SPI1 and I²C1:

| RP2040 GPIO | Arducam |
| --- | --- |
| GP9 | CS |
| GP10 | SCK |
| GP11 | MOSI |
| GP12 | MISO |
| GP2 | SDA |
| GP3 | SCL |

SPI test register readback must pass both 0x55 and 0xAA, and sensor ID must match
OV2640 before initialization. The required JPEG register tables are taken from
the official ArduCAM repository at commit
`066a7ea1ccae4813e9a9b9d04e55f96bb8b4039e`; their MIT license is retained in
`include/camera/LICENSE.ArduCAM`. Other sensor models report unsupported and are
not initialized with OV2640 settings.

Core 1 handles camera initialization, capture polling, and bounded FIFO reads
alongside the environmental sensor. Startup exposure settling is limited to the
camera initialization on core 1. The environmental sensor's sampling interval
is scheduled rather than sleeping between reads. Core 0 continues GPS, radio,
USB and PPS servicing. A 32 KiB image buffer bounds RAM use; oversize images and
capture timeouts report errors. Capture is 320 × 240 JPEG, first on startup and
then at 60-second intervals. No capture blocks waiting for a frame.

Only a complete JPEG is published to the USB sender. Core 0 sends at most one
48-byte image chunk per loop as base64 `CAM_DATA`, between `CAM_BEGIN` and
`CAM_END` lines, with frame ID, offset, total length and CRC32 integrity checking.
GPS/ENV/radio diagnostic lines can interleave. USB backpressure postpones chunks;
a stalled transfer is discarded after 15 seconds. No image is sent over LoRa.

The local bridge checks order, size, CRC, JPEG markers, 320 × 240 dimensions,
and actual JPEG decoding before making an image available. Only the latest good
image is retained in RAM; errors do not replace it with partial data. Metadata
and image age are exposed on `/api/gps`, and bytes at `/camera.jpg`. The dashboard
shows a small snapshot with age and one-image-per-minute cadence.

Hardware validation: SPI and sensor ID passed, and a real 7521-byte image was
received, decoded, and inspected visually. GPS retained a fix, environmental
readings remained available, and radio errors, timeouts, bad frames, event gaps,
and overflows remained zero during initial capture checks. This verifies the
local bench setup, not an established remote radio link or flight performance.
The next automatic capture was also received and decoded at the 60-second
cadence (7163 bytes). During that refresh, observed GPS age stayed below 0.98 s
and environmental sample age below 2.09 s, with the same radio counters at zero.
The RP2040 builds use about 49.5 KiB static RAM (19.3% of 256 KiB).

## Transport contract

Frames are delimited with 0x7E; 0x7E/0x7D bytes are escaped with 0x7D and XOR 0x20.
The unescaped frame contains version u8, kind u8, request/event ID u16, session
u32, payload length u16, payload and CRC-16/CCITT-FALSE u16. Integers are big-endian.
SNR is signed quarter-dB fixed point; native floats, size_t and C++ layouts never
cross the wire. The parser has a 320-byte limit and recovers at the next delimiter.
Writers enforce their actual destination-array capacities.

Commands cover handshake, initialization, status, configuration, RX, idle, data
gate/deadline, and typed data/control/beacon sends. Replies include actual radio
state, TX status, applied configuration and a radio-clock timestamp. RX events
carry observations and validated packets; state changes and hardware errors are
also reported. Valid data events preserve the original RF packet bytes.

Only one command is outstanding. Short commands use a **bounded synchronous
confirmation**, with a 350 ms timeout; this preserves existing LinkManager
semantics that configuration or RX has actually taken effect before returning.
RF transmission itself remains nonblocking: acceptance and TX completion are
separate. Neither side stops draining UART while serializing a frame. The RP2040
has a 1024-byte hardware-serial RX ring and 16 bounded deferred packet events.
Callbacks run from RemoteProtocol::poll, outside the command wait. As with the
local implementation, data sends from data callbacks return Busy.

No automatic command retransmission occurs after an uncertain result. The server
caches the last completed request ID/result and rejects stale IDs, preventing a
replayed command from repeating an RF send. Fresh host connection attempts use a
random session token, close the gate and discard old queued host events. Events
have monotonically increasing sequence IDs; gaps or queue overflow invalidate the
connection rather than silently giving LinkManager incomplete statistics.

Short handshake/status round trips estimate clock offset and uncertainty; large
packet commands do not perturb that estimate. RX timestamps retain radio-side
poll-detection time, translated to the host clock. Data deadlines are translated
to radio time and shortened by the measured uncertainty. ProtocolManager checks
its local deadline and actual airtime immediately before TX. This is approximate
clock synchronization, not interrupt-level RF timestamping or a hard real-time
latency guarantee; verify timing at the desired sustained traffic rate.

The host refreshes status and its clock estimate every 250 ms when polled. If the radio board
hears nothing valid for 1500 ms, it closes the data gate and idles after any active
TX finishes. Missing replies fault the host's remote service. The application
reconnects, reinitializes the radio and restarts link acquisition. Uptime arithmetic
and command/event sequence counters support wraparound.

## Validation and limits

`test/test_split` runs the production managers and actual UART parser/server/client
against byte-paced, finite-buffer UARTs and the existing simulated radio adapter.
It tests maximum packets, escaped bytes, corrupt/malformed reception, backpressure,
configuration rejection, deadline enforcement, timeout, lease expiry, radio reboot,
message delivery, millis wrap, two-endpoint acquisition and P0-to-P1 negotiation.
Both native scripts compile with AddressSanitizer/UBSan and warnings as errors.

Opt-in `split_radio_test` and `split_host_test` add a diagnostic decoder command.
It injects known valid/corrupt/malformed raw packets on the radio board, forwards
observations/packets to the real host managers, and checks application payloads.
The host's USB `t` command runs this test, ten physical local TX completions and
100 RX commands. It stops scheduling and leaves the radio idle. The diagnostic
command is absent from production radio firmware. A stack watermark is reported
by diagnostic radio firmware to measure unused SRAM during the tested paths.

```sh
pio run -e split_radio_test -e split_host_test
# Upload both *_test environments, then:
python tools/test-split-hardware.py
```

Single-device hardware tests cannot establish remote RF receipt, range, collision
handling or two-endpoint hardware negotiation. MessageManager now implements outbound fragmentation and bounded reassembly;
the original dated hardware report predates that source refactor. See the [dated test report](../test-results/2026-10-22-split/REPORT.md) for executed checks and results.

## Balloon and ground application boundaries

`src/split/host_main.cpp` is the shared RP2040 runtime: UART recovery, manager
initialization/polling, role-specific link scheduling, diagnostic commands, and
onboard LED heartbeat. The managers and radio coprocessor are shared between roles.
The application is selected at compile time:

| Build | Application | Peripherals |
| --- | --- | --- |
| `split_host`, `split_host_test` | `include/apps/BalloonApplication.h` | GPS, DHT22, camera; USB sensor feed and LoRa telemetry producer |
| `split_host_ground` | `include/apps/GroundApplication.h` | SD check, LCD demo, received telemetry USB feed; no balloon peripheral initialization |
| `split_radio` | `src/split/radio_main.cpp` | Same LoRa coprocessor firmware for either endpoint |

Application hooks own peripheral setup, sensor polling, work during data phases,
and core-1 peripheral work. LinkManager still owns all link timing and the packet
service enforces airtime deadlines. The ground build has only the NeoPixel
external library dependency; TinyGPSPlus and DHT are balloon-only dependencies.
The existing sensor helper headers remain in `include/` and are included only by
the balloon application. The typed MessageManager producer is shared through this application boundary.

The ground application registers a MessageManager consumer for complete
messages. MessageManager validates/decodes A1 records and delivers typed GPS to
the LCD. `GROUND_RX` emits complete small messages as exact hex for the Python
bridge, which exposes DHT and external RTD as distinct fields. An eight-line
bounded USB queue drains without blocking. Camera reassembly is supported by
the manager's callback API, but automatic camera RF transfer and ground image
storage/display are not enabled. See [MessageManager.md](MessageManager.md).

For initial ground acquisition, the application disables automatic data-profile
probing and negotiates P0. Shared status reports include conversation, profile,
missed control windows, signal measurements, P0 integrity counters and completed
message count. Application hooks configure this policy; balloon policy remains
unchanged. The radio firmware is identical for both roles.

Current ground ports are RP2040 `/dev/cu.usbmodem1201` (USB serial
503359276A727B9F) and BSFrance `/dev/cu.usbmodem1301`. Build/upload with:

```sh
pio run -e split_radio -t upload
pio run -e split_host_ground -t upload
pio device monitor -e split_host_ground
```

Check USB identities when moving cables; port names may change. Default
`split_host` remains the balloon application. Live P0 telemetry, beacon
acquisition, SYN/SYN-ACK/ACK and subsequent data reception were observed; see
`test-results/2026-10-22-ground-listen/REPORT.md`.

## RP2040 packet activity LED

Both host roles use the onboard GP16 RGB LED. Startup blinks white for two
seconds. After startup the LED is off when idle and indicates accepted TX
attempts or successfully decoded RX packets, regardless of direction:

| Packet | Color |
| --- | --- |
| P0 data | Bold red |
| P1 data | Red-orange |
| P2 data | Orange |
| P3 data | Yellow-green |
| P4 data | Bold green |
| Beacon, any profile | Yellow |
| Control, any profile | Blue |

Data packets use solid profile colors over the estimated TX airtime, with a
175 ms minimum indication. Received data shows a solid 175 ms indication after
decoding, because packet type/profile are unavailable before reception completes.
Beacon and control packets blink (100 ms on, 75 ms off) over the TX indication
or for one cycle after RX. Beacon yellow uses more red than green to compensate
for the onboard LED's green appearance. New activity replaces the previous indication.
The LED indicates local packet activity, not confirmed remote delivery or link
establishment. Updates do not wait or add radio traffic. The ground dashboard's
existing activity observer still receives the same packet notifications.

## Balloon telemetry pacing

The telemetry producer pauses after each packet's estimated transmission end:
P0 2000 ms, P1 1000 ms, P2 500 ms, P3 250 ms, P4 125 ms. Unknown modulation
uses the P0 pause. Start-to-start spacing is packet airtime plus that pause;
pauses are chosen from the radio settings when the packet is accepted. A rejected
send retains the 250 ms retry delay. LinkManager's data-window gate still prevents
a packet from overrunning the scheduled window. The application takes a fresh snapshot when the manager is ready. A snapshot
accepted into the bounded outbound slot may wait for a data gate to reopen;
there is no historical backlog or catch-up burst.
Sensor acquisition rates are unchanged, so faster telemetry can repeat readings.
These source changes require a balloon host firmware update to take effect.

## Ground activity observer

RemoteProtocol offers an optional `ActivityReceiver` callback with decoded RX
packet type/flags, captured profile, estimated RF start and airtime, and accepted
local TX attempts. The callback must not block or issue RPCs. GroundApplication
queues these as `GROUND_EVENT kind=packet` USB records. It also emits scheduled
window bounds from LinkManager and two-second snapshots for reconnection.
Snapshots do not affect link timing. Ground telemetry/event output shares an
eight-line queue; direct peripheral/status reports are deferred while it drains.
The dashboard bridge uses these records for its 60-second radio timeline.
No extra LoRa or board-UART packets are introduced by the observer.

## MAX31865 PT100 temperature sensor

Balloon firmware reads a two-wire PT100 through a MAX31865 on shared SPI1:
SCK GP10, MOSI GP11, MISO GP12, CS GP13 (camera CS remains GP9).
Both SPI peripherals run on core 1 with per-device SPI transactions; the
MAX31865 uses mode 1 and camera mode 0. CS pins idle high.
`RtdMonitor.h` uses a 100-ohm PT100 and assumes a 430-ohm reference resistor
on the breakout. Confirm the breakout resistor value before interpreting
absolute temperature. The breakout must also have its two-wire jumpers set.
USB emits `RTD OK` or `RTD READ_ERROR` every second with Celsius, Fahrenheit,
resistance, raw ADC count and fault bits. RTD readings are separate from the
DHT22 telemetry fields and the A1 producer sends the corrected external RTD
under its own identity. Raw resistance/ADC counts retain lead resistance.

The three-wire probe is operated in two-wire mode. A hard-coded
`TemperatureOffsetC = -0.89f` is added after temperature conversion, at all
temperatures; Fahrenheit is derived from corrected Celsius. USB includes
`uncorrected_c` and `offset_c=-0.89` for auditability. This targets -40 to -20 C:
a fixed 0.35-ohm series resistance produces +0.8845 to +0.8902 C error using
the standard PT100 Callendar–Van Dusen curve. The correction assumes one red
and the white wire carry the measurement current, equal lead resistances, and
that the reported 0.35-ohm red-to-red measurement excludes meter lead/contact
resistance. If both reds are connected in parallel, this estimate changes.
Cable resistance changes with cable temperature, so a room-temperature lead
measurement does not establish the actual offset when the cable itself is cold.
This is a fixed approximation, not a low-temperature calibration.
Model reference: https://www.analog.com/en/resources/technical-articles/rtd-measurement-system-design-essentials.html


## Internal low-temperature DS18B20 probe (GP26)

The balloon uses `InternalTemperatureMonitor.h`, OneWire 2.3.8 and
DallasTemperature 3.11.0 to read the internal probe. External MAX31865/PT100
remains on shared SPI1 with CS GP13 and its -0.89 C correction.

| Breakout/probe connection | RP2040 Zero |
| --- | --- |
| Signal / DQ | GP26 (GPIO label, not physical pin number) |
| VCC / VDD | 3.3 V |
| GND | Common GND |
| DQ pull-up | Approximately 4.7 kohm to 3.3 V, often fitted on the breakout |

The terminal-board label alone does not establish the probe model; firmware
requires one valid 1-Wire ROM with DS18B20 family byte 0x28 and correct ROM CRC.
More than one matching device is reported as ambiguous rather than silently
choosing a probe. A missing or failing device is rediscovered every five seconds.

Only core 1 uses 1-Wire. Resolution is set to 12 bits in volatile scratchpad
(EEPROM auto-save disabled). Conversions are asynchronous: allow 800 ms before
reading, with roughly one requested sample per second. DallasTemperature checks
scratchpad CRC and rejects disconnected/all-zero reads. The driver accepts
-55..125 C, applies no additional temperature offset and does not publish a
power-on scratchpad before requesting a conversion.

USB reports `INTERNAL_TEMP OK` or an explicit failure with pin, family ROM,
sequence and presence/idle-bus diagnostics. Valid samples less than six seconds
old enter MessageManager record ID 3 as signed hundredths C. The decoder exposes
`sensors.internal_low_temperature_c`; `environment.c` remains the DHT22 and
`sensors.external_rtd_c` remains the corrected external PT100. Unknown or stale
probe readings are omitted, never replaced with 0 C. The combined sensor
snapshot occupies 38 bytes when GPS, DHT and both probes are included.

This implementation is for a powered 3.3 V probe/breakout with a signal pull-up.
Manufacturer reference: https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf


## BMP280 pressure acquisition and LoRa telemetry

`PressureMonitor.h` replaces the earlier identity-only probe. It discovers the
BMP280 at 0x76 or 0x77 (chip ID 0x58) on shared Wire1, SDA GP2/SCL GP3, at
100 kHz. All I2C transfers are checked and serialized using BalloonI2c's mutex.
The connected module is at 0x76. Missing/failed devices retry after five seconds.

Core 1 reads factory calibration, configures normal mode (temperature x2,
pressure x16, IIR x4, 500 ms standby), and reads a coherent six-byte raw
pressure/temperature burst roughly every second. Hardware performs conversions;
the firmware does not sleep while holding the bus. Bosch's data-sheet
compensation equations use the factory coefficients and the chip temperature.
The compensation helper rejects uninitialized/sentinel ADC values and unusable
calibration. Its data-sheet reference vector is covered by native tests.

`BARO OK pressure_pa=... pressure_hpa=... chip_c=...` is the USB diagnostic.
Chip temperature is used for pressure compensation, not relabeled as one of the
three requested temperature probes. Valid pressure is sent in integer Pa under
MessageManager record ID 5 and decoded as `sensors.pressure_pa` on ground.
A valid complete six-record sensor snapshot occupies 44 application bytes.

Manufacturer operating bounds are 30000..110000 Pa and -40..85 C. Outside
these bounds the USB record says OUT_OF_RANGE and pressure is omitted from
LoRa, just as failed/stale sensor samples are omitted. In particular the BMP280
is not rated for pressures below 300 hPa at high balloon altitude. The packet
format itself can represent lower pressure for a future compatible sensor.
See the [Bosch data sheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf).

The earlier [identity check](../test-results/2026-10-23-bmp280/REPORT.md) preceded
pressure acquisition. Current deployment/testing is recorded in the
[all-sensor telemetry report](../test-results/2026-10-23-all-sensor-telemetry/REPORT.md).

### DS18B20 GP26 retest

After wiring adjustments and moving the probe back to GP26, the full balloon
firmware detected ROM 285FE4D500000051 and obtained repeated readings around
22.7..22.8 C. Configuration/read/request errors also occurred and recovered
automatically. Valid internal-probe values were included in MessageManager
telemetry. See the [retest report](../test-results/2026-10-23-ds18b20-gp26-retest/REPORT.md).


## Current balloon sensor telemetry coverage

| Sensor | Wiring | MessageManager record |
| --- | --- | --- |
| DHT22 internal temperature | GP8 data | 1, signed hundredths C |
| DHT22 internal humidity | Same GP8 device | 2, hundredths %RH |
| DS18B20 internal low-temperature probe | GP26 data | 3, signed hundredths C |
| MAX31865 external PT100 | SPI1 GP10/11/12, CS GP13 | 4, corrected signed hundredths C |
| BMP280 pressure | Wire1 GP2/3, address 0x76 | 5, integer Pa |
| MAX-M10S GPS | Same Wire1 GP2/3, address 0x42 | 6, position/altitude/satellites/status |

Only fresh, valid sensor fields are included. MAX31865 retains the -0.89 C
correction. GPS speed and UTC remain USB-only in the existing GPS schema.
Camera snapshots remain USB-only; the balloon producer never calls sendCamera.
`TELEMETRY QUEUED records=0x3F` indicates all six requested sensor records were
included; packet_result=0 means local radio acceptance, not remote receipt.
Matching ground firmware is built but must be deployed to ground hardware
before physical end-to-end decoding can be verified.
