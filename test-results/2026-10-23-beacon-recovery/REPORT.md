# Beacon-triggered ground recovery — 2026-10-23

Cause: LinkManager::onBeacon returned Ok without processing valid beacons when
established_ was true or phase_ was Control. A rebooted balloon's acquisition
beacons therefore could not supersede the old ground schedule.

Fix: every validated P0 acquisition beacon (nonzero countdown, supported version,
valid duration/length/modulation) discards old pending/established state, resets
acquisition/data-profile/probe state to P0, and adopts the advertised next control
boundary. Repeated beacons refine timing. Explicit manual profile proposals are
retained for the next negotiation. Invalid beacons do not alter the schedule.
Beacon packets have no session identity; recovery follows valid beacon timing,
not a claimed new session identifier. Existing packet formats are unchanged.

Validation:
- Regression demonstrated failure before the production change.
- Native tests pass, including established data phase, active WaitSynAck with
  pending agreement and P1 profile, invalid beacon rejection, repeated beacons,
  countdown across millis rollover, and balloon-only restart with encoded RF
  packets followed by a successful new handshake. See native-tests.log.
- Split-device simulation passes all 361 checks.
- split_host and split_host_ground PlatformIO builds succeed.
- Connected ground host (USB serial 503359276A727B9F) uploaded and flash verified.
- Dashboard bridge restored at localhost:8765; serial capture ground.log enabled.
- Ground host starts healthy in Searching/P0 with no radio/UART errors.
  A physical balloon power-cycle recovery has not yet been verified after upload.

Only the connected ground host was flashed. Balloon and radio MCU updates are
not required for this ground-side policy fix.
