# Intermittent balloon data and missing acquisition — 2026-10-23

## Findings

The evidence supports recurrent balloon UART/session failures as a mechanism for
delayed/missing beacons and unsynchronized data. The physical cause of those UART
failures is still unresolved. No claim of a hardware capacity limit or a completed
repair is justified.

Current USB identities, verified with pyserial before the upload attempt:

| Endpoint | USB serial | Port |
| --- | --- | --- |
| Balloon RP2040 Zero | 5303284748B66A1C | /dev/cu.usbmodem1401 |
| Ground RP2040 Zero | 503359276A727B9F | /dev/cu.usbmodem1201 |

The balloon PlatformIO default still pointed to the ground port. Corrected the
balloon default to the current port and documented its serial identity; future
connection changes still require identification before flashing.

## Physical observations

Read the existing ground dashboard bridge's serial log while it continued running;
did not open a competing reader or stop/reflash the ground station. The saved
ground-before.log is a snapshot of that bridge's accumulated session, not a new
capture starting at investigation time. Live tail reads confirmed ongoing traffic.

The continuing bridge log included 39 successfully received 44-byte messages at
the time of the size analysis, alongside shorter messages where sensor records
were omitted. A hard 44-byte payload ceiling is therefore inconsistent with this
observed reception. The 44 application bytes occupy 59 RF bytes including the
13-byte data header and two-byte CRC, below the protocol's 255-byte frame limit.
This does not exclude load-related or electrical failures.

The same log showed eight received beacons: six clustered at ground uptime
528845–537530 ms, then single beacons at 674071 and 885124 ms. Ground repeatedly
sent SYN packets, but no received control packet appeared in the analyzed log;
the reported link remained unestablished. Ground UART timeouts, malformed frames,
event gaps, overflows and radio-error counts stayed zero in the inspected reports.
Observed data-reception gaps had a median of about 5.8 seconds and a maximum of
62.4 seconds. Some silence is intentional: P0 packet airtime plus its 2-second
pause, and scheduled 10-second beacon/control windows, create normal gaps too.

Balloon USB yielded no diagnostics during 45-second and 25-second capture attempts,
including a status-command attempt and DTR toggle. Its USB port still enumerated.
The 1200-baud software bootloader reset/upload failed, and a serial-targeted
picotool force-reboot also failed to find an accessible device. No firmware was
successfully installed. The empty balloon-before.log records that lack of output,
not a clean UART result. A physical reset was unavailable because the user was
away from the hardware; further physical isolation is deferred.

The earlier all-sensor runtime.log contains 37 balloon UART timeouts with zero
reported bad frames/event gaps/overflows. Those are historical observations, not
fresh counters retrieved from the currently silent balloon USB connection.

## Reproduced software mechanism

RemoteProtocol::request marks the transport disconnected on a command timeout.
The host loop reconnects, initializes the radio and starts LinkManager again.
LinkManager::start always puts a balloon into a new 30-second data window, ahead
of its first beacon window. Recurrent disconnects can repeatedly reset that
countdown; a late solitary beacon can also advertise a control appointment that
is subsequently abandoned by another restart.

reproduce.cpp exercises production managers through the existing byte-paced
two-endpoint UART/RF simulator, using a complete 44-byte sensor message. It models
the host's reconnect/initialize/start behavior, including its 2-second retry timer.
One deliberately corrupted command every five seconds models a UART fault; it
does not model or identify the physical origin of that fault.

| 180-second simulation | UART timeouts | Link restarts | Data TX | Beacon TX | Ground established |
| --- | ---: | ---: | ---: | ---: | --- |
| Healthy UART | 0 | 0 | 24 | 6 | yes |
| Fault every 5 seconds | 36 | 30 | 30 | 0 | no |

See reproduce.log. This demonstrates the causal mechanism, not a diagnosis that
the connected hardware necessarily has the exact injected fault pattern.

## Prepared diagnostic changes

No schedule, RF format, sensor acquisition, UART baud or retry behavior changed.
RemoteProtocol now retains lastTimeoutOp, lastTimeoutAtMs and maxCommandRttMs
across reconnects. The host status line additionally exposes linkStarts,
maxLoopGapMs, uptimeMs and phaseEndMs. maxLoopGapMs includes normal command waits
and reporting work; it is not by itself proof of a sensor stall.

Operation IDs: 1 Hello, 2 Initialize, 3 Status, 4 Configure, 5 Receive, 6 Idle,
7 Gate, 8 SendData, 9 SendControl, 10 SendBeacon, 11 Diagnostic. lastTimeoutOp=0
means no recorded timeout. Failure context remains historical after recovery.

Balloon and ground builds passed; the split suite passed all 1379 checks under
ASan/UBSan, including the new failure-context retention checks. The reproduction
also ran under ASan/UBSan. Prepared balloon-diagnostics.uf2 SHA-256:
e52016569ae60c78b2e02f6c6918dbd9c9e25ccdee38d8c4f3711568087f07e7.
The image has not been deployed or physically verified.

## Next physical diagnostic

When hardware access resumes, first reset only the balloon RP2040 and capture its
existing firmware startup with the ground bridge still running. If USB recovers,
deploy the prepared diagnostic build to the verified balloon identity. Correlate
the first timeout operation, loop gap, reconnect and phase deadline with ground
beacon/control events. If practical, expose the balloon radio MCU's USB diagnostics
too, so rejected commands and UART frame errors can be distinguished from host
stalls and lost replies. A sensor-disabled A/B run and UART/power measurements
are subsequent isolation steps, not established fixes.

## Additional history supplied by the user

The user reports that automatic RP2040 uploads had worked routinely after initial
setup, and repeated manual BOOT/RESET recovery began during today's sensor work.
This is a regression symptom to investigate together with communication failures.

Review of the DS18B20 integration report reveals a documented software-caused
incident: an early standalone diagnostic used delayMicroseconds/sleep_us with
interrupts disabled and froze USB. That diagnostic was corrected to use busy waits
and a watchdog, and an ordinary full-firmware upload succeeded afterward. The
same report records another automatic-reset failure during the GP27 follow-up;
the later all-sensor deployment also required manual BOOT/RESET recovery.

The first documented recovery therefore has a known diagnostic-software cause,
while the later recurring failures remain unresolved. Do not conflate them or
infer that the corrected standalone diagnostic is still installed. Current
production sensor-library interrupt handling, multicore interactions, USB service,
and hardware/power effects remain candidates requiring isolation.

The installed Arduino core's SerialUSB::checkSerialReset implements automatic
BOOTSEL entry through the running USB stack on a 1200-baud DTR change. Failure of
that path does not establish boot-ROM damage: an unresponsive application USB
path can prevent automatic uploads while manual BOOT/RESET still works.
