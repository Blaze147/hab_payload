#!/usr/bin/env python3
"""Observe production split firmware without changing link scheduling."""
import argparse
import pathlib
import re
import time
import serial
from serial.tools import list_ports

parser = argparse.ArgumentParser()
parser.add_argument('--seconds', type=float, default=95)
parser.add_argument('--output', default='test-results/2026-10-22-split/production-soak.log')
args = parser.parse_args()
ports = list(list_ports.comports())
hosts = [p for p in ports if p.vid == 0x2e8a and 'RP2040' in (p.description or '')]
radios = [p for p in ports if p.vid == 0x239a and p.pid == 0x800c]
if len(hosts) != 1 or len(radios) != 1:
    raise SystemExit('Need one RP2040 host and one BSFrance radio')
output = pathlib.Path(args.output)
output.parent.mkdir(parents=True, exist_ok=True)
phases = set()
host_samples = 0
failures = []
radio_bad = []
with output.open('w') as log, serial.Serial(hosts[0].device, 115200, timeout=.05) as host, serial.Serial(radios[0].device, 115200, timeout=.05) as radio:
    start = time.monotonic()
    host.write(b's')
    while time.monotonic() - start < args.seconds:
        for name, port in [('HOST', host), ('RADIO', radio)]:
            line = port.readline().decode(errors='replace').strip()
            if not line:
                continue
            entry = f'{time.monotonic() - start:7.2f}s {name} {line}'
            print(entry, flush=True)
            log.write(entry + '\n')
            log.flush()
            values = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', line)}
            if name == 'HOST' and line.startswith('SPLIT_HOST'):
                host_samples += 1
                phases.add(values['phase'])
                if values['connected'] != 1 or values['state'] == 4 or values['phase'] == 5:
                    failures.append(entry)
                if any(values.get(k, 0) for k in ['radioErrors', 'timeouts', 'bad', 'eventGaps', 'overflow']):
                    failures.append(entry)
            if name == 'RADIO' and line.startswith('SPLIT_RADIO'):
                radio_bad.append(values['bad'])
    if host_samples < 2:
        failures.append('Missing host status samples')
    if radio_bad and max(radio_bad) != min(radio_bad):
        failures.append('Radio UART error count increased during observation')
    result = f'SOAK seconds={args.seconds:g} hostSamples={host_samples} phases={sorted(phases)} failures={len(failures)}'
    print(result, flush=True)
    log.write(result + '\n')
    if failures:
        raise SystemExit('\n'.join(failures))
