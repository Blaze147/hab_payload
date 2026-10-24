"""Read-only observation after an RP2040-only firmware deployment."""
import json
from pathlib import Path
import time
from urllib.request import urlopen

import serial
from serial.tools import list_ports

root = Path(__file__).resolve().parent
identity = '5303284748B66A1C'
ports = [p for p in list_ports.comports() if p.serial_number == identity]
assert len(ports) == 1
started = time.monotonic()
device = serial.Serial(ports[0].device, 115200, timeout=.1, exclusive=True)
pending = b''
snapshots = []
balloon_reports = []
next_sample = 0
last_uptime = None
monitor_closed = False
with (root / 'balloon-startup.log').open('w') as log:
    while time.monotonic() - started < 120:
        elapsed = time.monotonic() - started
        if elapsed < 15:
            pending += device.read(4096)
            while b'\n' in pending:
                raw, pending = pending.split(b'\n', 1)
                line = raw.decode('ascii', errors='replace').rstrip('\r')
                log.write(f'{elapsed:.3f} {line}\n')
                if line.startswith('SPLIT_HOST '):
                    balloon_reports.append(dict(p.split('=', 1) for p in line.split()[1:] if '=' in p))
            log.flush()
        elif not monitor_closed:
            device.close()
            monitor_closed = True
            print('Closed balloon USB monitor at 15s; no command or heartbeat bytes sent.', flush=True)
        if elapsed >= next_sample:
            with urlopen('http://127.0.0.1:8765/api/gps', timeout=3) as response:
                state = json.load(response)
            state.get('activity', {}).pop('events', None)
            state['observation_seconds'] = round(elapsed, 2)
            snapshots.append(state)
            uptime = (state.get('telemetry') or {}).get('balloon_uptime_ms')
            if uptime is not None:
                assert last_uptime is None or uptime >= last_uptime, 'Balloon uptime decreased after deployment'
                last_uptime = uptime
            print(json.dumps({'seconds': round(elapsed), 'messages': state['received_messages'],
                              'age_seconds': state['age_seconds'], 'balloon_uptime_ms': uptime,
                              'radio': {k: state.get('radio', {}).get(k) for k in
                                        ('connected', 'established', 'dataProfile', 'restartFlagSupported', 'timeouts', 'bad')}}), flush=True)
            next_sample += 10
        time.sleep(.1)
device.close()
with urlopen('http://127.0.0.1:8765/api/gps', timeout=3) as response:
    final = json.load(response)
final.get('activity', {}).pop('events', None)
assert final['connected'] and final['received_messages'] > snapshots[0]['received_messages'] + 3
assert final['radio']['connected'] == '1' and final['radio']['established'] == '1'
assert final['radio']['restartFlagSupported'] == '0'  # Radio firmware intentionally unchanged.
assert balloon_reports and balloon_reports[-1]['restartFlagSupported'] == '0'
assert final['age_seconds'] < 60
(root / 'observation.json').write_text(json.dumps({'duration_seconds': 120,
    'balloon_usb_monitor_closed_at_seconds': 15, 'heartbeat_or_fault_commands_sent': 0,
    'balloon_reports': balloon_reports, 'ground_snapshots': snapshots, 'final': final,
    'result': 'PASS'}, indent=2) + '\n')
print('PASS: 120-second observation, radio compatibility and RF reception; balloon monitor closed at 15s.', flush=True)
