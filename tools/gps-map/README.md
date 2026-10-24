# Live GPS map

## Ground station: live balloon telemetry

The existing dashboard also supports the ground station's received LoRa sensor
data. Start it with the ground RP2040's USB identity so it follows that board
across USB port changes:

```sh
~/.platformio/penv/bin/python tools/gps-map/server.py --source ground --serial-number 503359276A727B9F
```

Open <http://127.0.0.1:8765>. The route is balloon sensors → LoRa → ground radio
→ UART → ground RP2040 → USB → local Python bridge → browser. No direct USB
connection to the balloon is required. The ground runs `split_radio` and
`split_host_ground`; its USB port is currently `/dev/cu.usbmodem1201`.

`GROUND_RX` records are decoded from the exact 32-byte HABT-v1 payload hex,
with schema, length, identity, validity flags, and coordinate/sensor range checks.
The dashboard shows balloon position, altitude, satellites, Fahrenheit temperature,
relative humidity, RF signal strength, message count, message identity, age, and
link acquisition/establishment. Fields absent from the radio schema (speed, PPS,
UTC) are not invented. Invalid environmental readings retain the last good
temperature and humidity, with their age measured from that good update and a
separate latest-reading warning. Stale or disconnected readings remain visible
with a status note; values appear only after the first valid reading.
Ground mode ignores direct `GPS`, `ENV`, and camera lines; the camera card is
hidden and `/camera.jpg` returns 404, so old local camera images cannot appear
as received radio data.

Ground readings become stale after 60 seconds without a decoded RF telemetry
message; USB status lines do not refresh that age. This allows normal P0 airtime
and beacon/control-window pauses. A stale map marker retains its last location
with a muted color and a last-fix note; stale sensor values are unavailable.
Environmental values remain visible when stale. The UI polls the local API every 500 ms. The server binds only to localhost
and retains only the latest readings, without an SD or server-side history log.

The `--source balloon` option keeps the original direct-USB sensor/camera mode;
`--source auto` (default) selects based on the first recognized sensor feed.
Restart the bridge when changing endpoint roles. Use `--serial-port` instead of
`--serial-number` to select an explicit port. Only one bridge/monitor should own
the device's USB serial connection.

Run bridge tests (including the existing camera regression tests):

```sh
~/.platformio/penv/bin/python -m unittest discover -s tools/gps-map -p 'test_*.py' -v
```

## Original direct-USB balloon mode

Run the local USB bridge and open <http://127.0.0.1:8765>:

```sh
python3 -m pip install -r tools/gps-map/requirements.txt
python3 tools/gps-map/server.py
```

Or use the existing PlatformIO Python (install the camera decoder once):

```sh
~/.platformio/penv/bin/python -m pip install -r tools/gps-map/requirements.txt
~/.platformio/penv/bin/python tools/gps-map/server.py
```

The RP2040 is detected by USB identity. Override with `--serial-port /dev/cu.usbmodem1201`.
Close other serial monitors before starting; the bridge owns the serial port.
It reads the existing `GPS` lines without issuing device commands. Reconnection is
automatic. The marker becomes muted when the feed/fix is stale. Dragging the map
disables automatic centering; the center button restores it.

The server binds only to localhost, stores no position history, and requires
internet access for OpenStreetMap tiles. The displayed point is the receiver's
reported position, with its normal GPS uncertainty. Stop with Ctrl+C.

Leaflet 1.9.4 is vendored under its BSD license in `static/vendor`.

The bridge also parses `ENV` lines from the DHT22 on GP7 into `environment`
and `environment_age_seconds` on `/api/gps`. Dashboard temperature (°F) and relative
humidity (%RH) update about every 2.1 seconds. Invalid readings are unavailable,
and samples older than six seconds are stale. The sensor card is independent
of whether GPS currently has a fix.

The Arducam snapshot card displays a **320 × 240 JPEG once a minute**, streamed
over USB. The camera was identified by sensor ID `0x2641` (OV2640), independent
of the board's UC-C78 marking. Camera pins: GP9 CS, GP10 SCK, GP11 MOSI, GP12 MISO,
GP2 SDA, GP3 SCL. The bridge exposes metadata as `camera` on `/api/gps` and the
latest verified JPEG at `/camera.jpg`. Only the most recent image is kept in RAM;
there is no image history. It may take up to a minute to receive an image after
restarting the bridge. Reconnection and incomplete/corrupt transfers retain the
last verified snapshot with its age visible. JPEGs are CRC-checked and decoded
with Pillow before publication. Camera capture and transfers never use LoRa.

Run camera framing/integrity tests:

```sh
~/.platformio/penv/bin/python tools/gps-map/test_camera.py
```

The split balloon host also broadcasts a latest telemetry snapshot through the
existing LoRa data windows. USB GPS/ENV/JPEG output remains independent. LoRa
payloads are single-fragment 32-byte binary messages. Multi-byte integers are
big-endian; signed fields use two's complement:

| Offset | Field |
| --- | --- |
| 0–3 | ASCII HABT |
| 4 | Schema version 1 |
| 5 | Validity bits: 0 GPS stream, 1 position, 2 altitude, 3 satellites, 4 environment |
| 6–7 | Reserved, zero |
| 8–11 | Sender uptime in ms, uint32 |
| 12–15 | Latitude in millionths of a degree, int32 |
| 16–19 | Longitude in millionths of a degree, int32 |
| 20–23 | Altitude in tenths of meters, int32 |
| 24–25 | Temperature in tenths of Celsius, int16 |
| 26–27 | Humidity in tenths of percent RH, uint16 |
| 28–31 | Satellite count, uint32 |

Validity bits govern fields; a cleared bit means unavailable, and its fields
are zero. Environment samples expire after six seconds; GPS after three seconds.
The fixed short frame avoids the long UART burst failures seen on the physical
radio board with the initial ASCII payload. The exact cause of those longer-frame
failures remains unisolated.
The host attempts one snapshot five seconds after the previous accepted packet's
estimated completion; beacon/control slots pause telemetry. Busy or window-boundary
refusals retry after 250 ms with fresh values. There is no backlog or delivery
acknowledgement. Session/message IDs identify packets; the session starts randomly
at boot and advances when the message ID wraps. `TELEMETRY TX_ACCEPTED` on USB
means local acceptance only. Camera images remain USB-only. The ground host build
does not originate telemetry. A remote receiver must use the existing packet
protocol and matching radio profile to receive these broadcasts; no established
handshake is required for the balloon's initial P0 data transmissions.

The RP2040 Zero's onboard RGB LED (GP16) flashes dim orange every 400 ms for
its first two seconds, then pulses green for 100 ms once a second. This is an
application heartbeat, independent of USB, GPS fix, or LoRa link establishment.
LED updates run from the main loop without delay-based waits.

## Live radio activity strip

The translucent strip above the map displays the last 60 seconds of ground-host
events. Orange capsules are valid data packets, blue capsules are control packets
(S = SYN, SA = SYN-ACK, A = ACK), and yellow capsules are acquisition beacons.
Packets carrying the sender-restarted flag override those colors with dark red.
Their tooltip adds “Sender restarted (RP2040 or radio)”; either board can drive
the same flag. A cold power-on also sets it. Only the first eligible transmission
is marked: data or control with no other flags. Beacons and SYN/SYN-ACK/ACK keep
their normal colors and leave the restart indication pending. A lost marked
packet can hide the indication. This is independent of the message
session ID and does not imply a telemetry or link-handshake failure. Updated
RP2040 and LoRa firmware are required on both endpoints for restart flagging.
An updated RP2040 can still run telemetry with older LoRa firmware, but will
report `restartFlagSupported=0` and cannot yet mark restart packets. See
[the protocol definition](../../docs/ProtocolManager.md#sender-restarted-flag).

All packets share one row; packet tooltips identify their direction. Gray
vertical dividers mark observed link-window changes. Subtle moving ruler ticks
keep time visibly progressing during radio silence or bridge disconnection.

Capsule widths follow estimated LoRa airtime, with a three-pixel visual minimum
for very short packets. Hover a capsule for direction, packet type, profile and
airtime. A TX capsule grows while its known-duration transmission is in progress.
RX capsules appear only after successful decode and are backdated across their
estimated airtime; no in-flight RX or missing packet is invented. Ground TX
indicates local acceptance, not confirmed balloon delivery. Undecodable CRC
errors are not mislabeled as data/control/beacons. Timing is based on host
timestamps and the radio's airtime calculation, not an RF hardware timestamp.

This requires the ground RP2040 event-observer firmware. The balloon and radio
coprocessor need no changes for this feature. LinkManager scheduling and profile
policy are unchanged. The observer uses the bounded, nonblocking ground USB
queue shared with received telemetry, and periodic status/peripheral reports
wait for that queue to drain to avoid interleaving lines. Window heartbeats
initialize reconnecting dashboards but do not draw additional dividers.
Repeated beacon countdown refinements update the same acquisition interval.

`/api/gps` includes `activity`: a bounded list (maximum 512 events) pruned to the
60-second visible history, current window metadata, a server-clock anchor and
sequence-gap counters. The gap badge applies to gaps in the visible history;
the cumulative count is available as `total_dropped_events`. Host reboot starts
a new event generation. Packet or window events do not refresh balloon GPS or
sensor freshness. No radio commands are sent by the visualization.

For diagnostics only, `--serial-log PATH` captures raw USB lines. It is disabled
by default; production history is bounded in memory. The frontend is animated
at up to 30 fps (four updates/second with reduced-motion preference), independently
of the half-second status fetches.

## Browser position trail

The map records a valid fresh GPS fix immediately, then the next observed fix
at least ten minutes after the previous trail point. Only the last three hours
of trail points are retained (at most 18); reception gaps are not interpolated.
Dots age from green to red, updating even while disconnected. The larger current
marker stays green with a fresh fix and turns gray when stale or disconnected.

Trail points and the separate last known location are saved in `localStorage`
under `hab.position-history.v1`, and restored before the first API response.
The last known location remains available even after trail points expire.
Storage belongs to this browser profile and origin: use the same hostname and
port after refreshing. Clearing site data or private browsing can remove it.
If storage is unavailable, the dashboard displays a warning and continues in memory.
No server-side position history is added. The bridge must still serve the web
app on refresh; this does not add an offline app cache or offline map tiles.

Run browser history regression checks with:

```sh
node tools/gps-map/test_position_history.cjs
```
