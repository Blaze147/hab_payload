# Physical validation

1. Attach suitable 915 MHz antennas to BOTH boards before powering/transmitting.
2. Verify the actual board/module band, antenna gain, equipment authorization and
   permitted operating settings before RF testing. United States operation at
   915 MHz alone does not establish compliance. In particular, the default
   fixed-frequency 125 kHz P0/P1 examples are not certified Part 15 profiles.
   Part 15.247 digital modulation provisions include a minimum 500 kHz 6 dB
   bandwidth; hopping systems have separate requirements. Consult the actual
   equipment authorization and applicable rules or use an appropriate shielded
   laboratory setup. Changing the interval alone does not establish compliance.
   Reference: https://www.ecfr.gov/current/title-47/section-15.247
3. Transfer tracked source/config/docs/tools via Git or copy the workspace. Do not
   copy `.pio`; PlatformIO resolves the pinned LoRa dependency. Keep the existing
   project and board definitions. PlatformIO's VS Code terminal supplies `pio`;
   on this development Mac its executable was `~/.platformio/penv/bin/pio`.

## Build and upload

Hardware tests are opt-in. Default `transmitter` and `receiver` builds initialize
RadioManager and stay idle; they contain no hardware test application or test
payload buffer. Plain `pio run` builds only these two default environments.
Select an explicit environment below to compile and upload test behavior.
The native simulator/tests remain host-only.


| Device / mode | Environment | Default port |
| --- | --- | --- |
| Balloon one-way | transmitter_test | COM5 |
| Ground one-way | receiver_test | COM4 |
| Balloon exchange | transmitter_bidirectional | COM5 |
| Ground exchange | receiver_bidirectional | COM4 |

Select the appropriate environment in PlatformIO and Build, then Upload.
Equivalent commands:

```sh
pio run -e transmitter_test
pio run -e receiver_test
pio run -e transmitter_test -t upload --upload-port COM5
pio run -e receiver_test -t upload --upload-port COM4
pio device monitor -e transmitter_test --port COM5 --baud 115200
pio device monitor -e receiver_test --port COM4 --baud 115200
```

Use the actual ports on the test machine; USB bootloader ports may change.
Close monitors before uploading. Monitor is 115200 baud, 8N1, over USB CDC.
Startup does not wait for a monitor, so reconnect/reset if its first messages
were missed. `Initialization: Ok` and `Profile P0` indicate successful setup.
Failures print meaningful status and retry initialization after five seconds.

The balloon transmits six binary bytes: uint32 counter little endian, then int16
215 little endian (21.5 C). This is an application diagnostic fixture only.
Counters begin at zero after boot. TX request acceptance and local completion
are logged separately. Ground prints hex bytes, length, RSSI, SNR and counter.
Expected first packet: `00 00 00 00 D7 00`, length 6. Both sides must match
frequency, bandwidth, SF, coding, header mode, CRC, sync word and IQ settings.

## Recommended sequence

1. Confirm both boards initialize. Start ground first; attach monitors before
   resetting balloon so early packets are included in the record.
2. Send 10 packets and verify exact bytes, counters 0–9 and measurements.
3. Send 1,000 packets (0–999), save the ground log, and count unique counters,
   missing counters and duplicates separately. Local TX completion is not a
   reception count. Record received/1000; investigate any missing data rather
   than declaring the link validated. There are no retransmissions.
4. Repeat using P1 on BOTH devices. Compare settings and reception records.
5. Upload both bidirectional environments. Balloon sends then listens for up
   to 30 seconds; ground replies with the same diagnostic payload after one
   second. Verify RX at both ends and alternating TX/RX. Timeouts are logged;
   the next test packet proceeds without a retry protocol. This is not the
   production scheduler, acknowledgment system, or a delivery guarantee.
6. Restart each board independently. Verify initialization and communication
   recover without source changes; counter restart is expected. Repeat exchanges
   with ground restarted while balloon is awaiting a reply.

The default one-way loop repeats indefinitely, waiting 5,000 ms AFTER TX
completion. For a finite run append `-D TEST_PACKET_LIMIT=10UL` or `1000UL` to
transmitter_test build_flags, preserving its role and HARDWARE_TEST flags. Append `-D TEST_PROFILE=1`
to BOTH environments for P1, and optionally `-D TEST_INTERVAL_MS=10000UL` to
transmitter. For exchange preserve `-D BIDIRECTIONAL` too. Rebuild/upload both
after changing profiles. Exact alternative settings live in `profile()` in
src/hardware_test/HardwareTestApp.cpp; align both radios when modifying them. These test controls require
no changes to RadioManager. Profile selection remains manual in Stage 1.
P0 six-byte airtime is approximately 1.19 seconds; total 1,000-packet run is
roughly 103 minutes at the default interval. Set intervals and profiles only
within the operating authorization determined above.

## Failures to report

Save full logs from both devices with environment, Git revision, ports, profile,
antenna details, distance, packet limit, unique received count and missing counter
ranges. Initialization failure suggests power/SPI/pin/module mismatch. HardwareError
or Timeout can indicate device/bus failure. TX complete without RX can indicate
mismatched settings, antennas, RF conditions or wrong firmware role. Repeated
Response timeout is a failed exchange test. Simulation cannot diagnose these
physical causes. Verify power, board variant and matching settings first.
