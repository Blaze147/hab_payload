# Live automatic profile negotiation — 2026-10-22

## Uploaded hardware

- Ground RP2040 Zero, USB serial 503359276A727B9F, /dev/cu.usbmodem1201:
  production `split_host_ground` firmware, flash verification passed.
- Ground BSFrance/Feather 32u4 radio, /dev/cu.usbmodem1301:
  production `split_radio` firmware, 21456 flash bytes verified.
- Balloon remained separately powered, with its existing firmware. It was not
  connected, flashed, or manually commanded to a particular profile.

The policy/statistics changes execute on the RP2040. The ground radio update was
also required by the earlier addition of airtime to UART observation frames.
The LoRa packet format remains compatible with the existing balloon firmware.
The new balloon telemetry pauses are not deployed; the balloon retains its old
five-second pause plus packet airtime.

## 420-second live RF observation

Ground initially listened on P0 while the previously established balloon timed
out its old control relationship. A beacon synchronized ground at 156.03 s.
The P0 control handshake established the link, and startup automatic exploration
negotiated every higher data profile without intervention.

Times below are first sampled ground agreement statuses (provisional final ACK
completion), relative to capture start. Subsequent real balloon data confirmed
that the balloon changed to each profile. This is not just local radio retuning.

| Profile | First agreement status (s) | Observed airtime goodput (bytes/s) | Captured telemetry messages |
|---|---:|---:|---:|
| P0 | 200.19 | 9.74 | 24 |
| P1 | 240.21 | 48.63 | 6 |
| P2 | 280.19 | 103.56 | 6 |
| P3 | 320.23 | 363.64 | 6 |
| P4 | 360.19 | 1333.33 | 10 |

- 52 captured telemetry messages, IDs 900 through 951, same balloon session 7405,
  no gaps or duplicates in that captured sequence.
- 210 host status samples. Host radioErrors, timeouts, bad UART frames,
  eventGaps, overflow and missedControlWindows remained zero.
- Radio UART bad counter was already 1 at capture start and remained 1; radio
  rejected commands stayed zero. This was not a clean-zero boot counter run.
- Startup exploration ended at P4. A subsequent P0 SYN/SYN-ACK/ACK exchange
  completed, and P4 data reception resumed afterward.
- Record IDs advanced 1 through 5 with actual data-profile changes. Control
  slots retained the same record. Recent summaries reached four packets;
  whole-instance summaries continued accumulating separately.
- Final sampled state: established=1, P4, instance=5, probing=0, missed=0.
  The final P4 telemetry message arrived at 419.06 s.
- Monitor verdict: `SOAK seconds=420 hostSamples=210 phases=[1, 2, 4] failures=0`.

## Additional verification and limits

Native ASan/UBSan regression passed 17,240,393 checks; split simulation passed
345 checks. Simulation covers failed probe negotiation across multiple control
windows, evidence-trigger hysteresis, silent-window P0 recovery, cumulative
versus recent degradation detection, recovery resets, profile record retention,
and retrying failed downgrade handshakes.

The live RF test proves startup acquisition, automatic upward exploration,
profile changes on both endpoints, P0 control rendezvous while using faster data
profiles, and subsequent P4 telemetry. No RF-loss/degradation condition was
physically induced. The 16-minute periodic timer and evidence-driven retry/failure
paths were verified in simulation, not exercised end-to-end on RF by this
seven-minute capture. There was no failure to diagnose in the exercised live path.

## Dashboard restored

The dashboard bridge was stopped during direct serial capture, then restarted
with its original ground-source command and USB identity. HTTP /api/gps confirms
connected ground telemetry, P4, established=1, and fresh RF data. Firmware keeps
running after capture; no monitoring process is holding either serial port.

See live.log, summary.json, both upload logs, native-tests.log, split-tests.log,
dashboard-before.json, dashboard-after.json and firmware-manifest.json. The
restarted bridge logs to dashboard-server.log; its PID is in dashboard-pid.txt.
