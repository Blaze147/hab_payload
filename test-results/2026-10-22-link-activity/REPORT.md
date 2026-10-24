# Live radio activity strip — 2026-10-22/23

Implemented and deployed a translucent 60-second timeline above the Leaflet map.
Upper lane: balloon -> ground. Lower lane: ground -> balloon. Orange capsules
represent decoded data; blue represent controls (S/SA/A); yellow represent
beacons. Capsule width uses estimated LoRa airtime, with a three-pixel minimum.
Gray bars mark window boundaries. The independent animation and moving time ticks
continue through silence and disconnection. Hover/tap exposes packet details.

## Live deployment and verification

Only the ground RP2040 Zero (USB serial 503359276A727B9F) was updated. Upload and
flash verification passed. Balloon and radio coprocessor were not reprogrammed.
The observers introduce no new LoRa or board-UART packets and do not change link
policy or scheduling. Firmware queues packet and window events without waiting
for USB. The bridge was restarted to read that event stream.

Live API capture observed orange data, yellow acquisition beacons, and all three
control flags, in both TX/RX directions. Actual SYN -> SYN-ACK -> ACK exchanges
were visibly rendered, followed by data on the negotiated profiles. Raw captured
packet profiles include P0 through P4. The ground returned to established P4.
P0 data airtime was 3285 ms, P1 658 ms, P2 309 ms, P3 88 ms and P4 24 ms;
these differences are reflected in the rendered widths (subject to the minimum).
The SYN-ACK tooltip was verified in the browser: balloon -> ground, P0, 1.45 s.
No fake events were injected into the live dashboard.

Final validated session: radioErrors=0, timeouts=0, bad UART frames=0,
eventGaps=0, overflow=0, missed control windows=0, activity sequence gaps=0.
The initial integration capture exposed status starvation when heartbeat and
status periods aligned; the status scheduler now retries after the output queue
drains instead of postponing for another full period. Acquisition countdown
refinements and window heartbeats are coalesced, not drawn as extra boundaries.
Initial reconnect gaps are preserved in diagnostic totals; any warning badge
ages out with the 60-second visible history.

## Validation

- Python suite: 23 tests passed (event parser, airtime/direction, malformed input,
  long packet retention, clock wrap/reboot, bounded retention, idle clock,
  sequence gaps, heartbeat/countdown coalescing, freshness, existing telemetry
  and camera regressions).
- Split ASan/UBSan simulation: 361 checks passed, including success-only TX/RX
  observer events and rejecting phantom TX events on Busy.
- Native link regression: 17,240,393 checks passed.
- Both production RP2040 firmware builds passed.
- Browser: final layout and packet tooltip verified; no page console errors.

## Retention and timing

The server stores at most 512 events pruned to the latest 60 seconds, plus current
window metadata. The RP2040 uses a bounded shared eight-line telemetry/event
queue. The current server was restored without raw serial logging, so production
does not retain an unbounded capture. Observer timing is estimated from host
receive/command timestamps and LoRa airtime, not an RF hardware timestamp. RX
capsules are backdated after decode; TX means local acceptance, not remote
receipt. Undecodable packets are not assigned a fictional packet type.

Files: dashboard-activity.jpg, packet-tooltip.jpg, validated-api.json,
observed-events.json, serial.log (finite diagnostic capture), live-api.jsonl,
summary.json, build.log, upload.log, python-tests.log, split-tests.log,
native-tests.log, dashboard-server.log and server-pid.txt.
