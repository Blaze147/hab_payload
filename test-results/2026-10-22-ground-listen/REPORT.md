# Ground station live balloon reception — 2026-10-22

Ground station is running split_radio on the BSFrance LoRa32U4 II and
split_host_ground on RP2040 Zero. Both uploads verified flash. Balloon hardware
was elsewhere, powered on, and was not reprogrammed during this test.

## Configuration

- Ground RP2040: USB serial 503359276A727B9F, /dev/cu.usbmodem1201.
- Ground radio: VID:PID 239A:800C, /dev/cu.usbmodem1301.
- Board UART: RP2040 GP0/TX -> radio D0/RX; GP1/RX -> D1/TX;
  common ground, 100000 baud. Ground radio's older balloon P0 test firmware
  was replaced with the shared production radio coprocessor firmware.
- RF: 915 MHz, P0 SF12 / 125 kHz / coding rate 4/8, sync word 0x12,
  explicit header and CRC. Ground automatic data-profile probing disabled.
- SD remains on SPI1 GP9/10/11/12 and passes the 1024-byte check with 5V
  adapter supply. LCD remains on I2C1 GP2/GP3 at 3.3V with scrolling demo.

## 130-second hardware capture

See [live.log](live.log).

- 20 valid P0 receive observations: 13 data messages, six acquisition beacons,
  and a SYN-ACK. Message IDs 293 through 305 inclusive, transmitter session 7405.
  Each data payload was a 32-byte HABT-v1 telemetry record; no image messages.
- First telemetry arrived at 8.53 s. Beacon-driven synchronization was visible
  by 69.17 s (phase Searching -> Data, before link established).
- Control exchange observed: SYN sent by 107.17 s, waiting for SYN-ACK by
  109.17 s, received reply and sending ACK by 111.17 s. Ground reported
  established=1 and conversation=7 (Provisional) by 113.17 s.
- Provisional is the correct ground-side state after local ACK completion;
  ground cannot independently prove balloon receipt of the final ACK.
- Ground returned to Data at 117.20 s; subsequent telemetry arrived at
  120.25 s and 128.54 s. Final status: established=1, dataProfile=P0, missed=0.
- Ground host counters remained zero for radioErrors, timeouts, bad UART frames,
  event gaps and overflows. P0 corrupt/malformed counts remained zero.
- Radio bad-frame counter was already 1 at capture start and remained 1;
  rejected commands stayed zero. It was not a clean-zero radio counter run.
- Final RSSI -43 dBm and SNR 8.5 dB. Valid payload fields in the final record:
  GPS 39.563668, -105.016780; altitude 1717.5 m; 11 satellites;
  temperature 25.7 C; relative humidity 40.6%; balloon uptime 3048367 ms.
  Two earlier records had invalid environment flags; decoder omitted those
  values rather than treating raw zeros as valid measurements.

## Implementation and validation

GroundApplication installs a MessageManager receiver, configures P0 acquisition,
and retains ground-only peripherals. Shared runtime now invokes application
initialization with MessageManager and an application link-policy hook. Balloon
hook preserves its existing policy. Status includes handshake, profile, signal,
message, and P0 integrity counters. Bounded USB queue preserves raw payload hex
and adds HABT-v1 decoding. Only complete single-fragment messages are supported;
SD logging and image reassembly are not implemented by this change.

Builds passed: split_radio, split_host_ground, split_host. Live uploads verified.
ASan/UBSan split simulation: 335 checks passed, including two split endpoints
with four independent clocks and the full handshake. Native regression:
17,239,993 checks passed; telemetry window test delivered 19/19 messages and
preserved handshakes; integrated P0 simulation delivered 30/30 packets.
See build.log, split-tests.log, native-tests.log, and upload logs.

Ground firmware continues listening after the capture program closes. No SD
telemetry log was created; captured RF data is saved on this computer in live.log.
The older GPS-map server was temporarily suspended during serial capture and
resumed afterward; it is not a dashboard for this ground RF feed.

## Ground telemetry web dashboard

Adapted tools/gps-map to receive GROUND_RX payloads. The bridge decodes exact
HABT-v1 wire bytes (not the diagnostic suffix) and maps valid remote GPS and
environment fields to the existing API. UI labels identify balloon -> LoRa ->
ground -> browser, with RF RSSI/message counts in place of unavailable speed/PPS.
Camera is excluded in ground mode. Invalid environmental readings are unavailable,
and 60-second RF freshness is independent of continued USB status traffic.

The previous direct-USB bridge process was replaced by a ground-mode process:

```
/Users/james/.platformio/penv/bin/python tools/gps-map/server.py --source ground --serial-number 503359276A727B9F
```

The server follows the ground USB identity and remains running on
http://127.0.0.1:8765. Browser verified the live map, coordinates, altitude,
satellites, -44/-45 dBm RSSI, increasing message count, 78.1 F temperature,
and 40.6% RH. Camera card is hidden. Browser console contained no errors.
Saved screenshot: dashboard.png.

The HTTP API live check observed message count advancing 7 -> 8 and changing
coordinates. An invalid environment flag in the first sample cleared values;
the next valid packet restored 25.7 C / 40.6% RH. See dashboard-live-samples.json.

Twelve Python tests passed: live-payload decoding, signed temperatures, valid zero
coordinates, invalid flags clearing old values, malformed/range rejection, using
wire bytes rather than the suffix, ground ignoring local USB sensor/camera
lines, RF age unaffected by status heartbeats, preserved balloon mode, and four
camera integrity regressions. See dashboard-tests.log.
