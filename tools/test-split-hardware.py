#!/usr/bin/env python3
"""Run opt-in diagnostic firmware tests; select boards by USB identity, not port order."""
import argparse
import pathlib
import re
import time
import serial
from serial.tools import list_ports

parser = argparse.ArgumentParser()
parser.add_argument('--output', default='test-results/2026-10-22-split/hardware-selftest.log')
args = parser.parse_args()
ports = list(list_ports.comports())
hosts = [p for p in ports if p.vid == 0x2e8a and 'RP2040' in (p.description or '')]
radios = [p for p in ports if p.vid == 0x239a and p.pid == 0x800c]
if len(hosts) != 1 or len(radios) != 1:
    raise SystemExit('Need exactly one running RP2040 host and one BSFrance radio board')
path = pathlib.Path(args.output)
path.parent.mkdir(parents=True, exist_ok=True)
with path.open('w') as log, serial.Serial(hosts[0].device, 115200, timeout=.05) as host, serial.Serial(radios[0].device, 115200, timeout=.05) as radio:
    def record(text):
        print(text, flush=True)
        log.write(text + '\n')
        log.flush()
    record(f'HOST {hosts[0].device} {hosts[0].hwid}')
    record(f'RADIO {radios[0].device} {radios[0].hwid}')
    host.write(b'xt')
    deadline = time.monotonic() + 40
    result = None
    while time.monotonic() < deadline:
        for name, port in [('HOST', host), ('RADIO', radio)]:
            line = port.readline().decode(errors='replace').strip()
            if line:
                record(f'{name} {line}')
                match = re.search(r'SELFTEST passed=(\d+) failed=(\d+)', line)
                if match:
                    result = int(match.group(2))
        if result is not None:
            host.write(b's')
            for _ in range(8):
                line = host.readline().decode(errors='replace').strip()
                if line:
                    record(f'HOST {line}')
            break
    if result is None:
        raise SystemExit('No SELFTEST result: check that both boards run *_test firmware')
    if result:
        raise SystemExit(f'Hardware self-test failed {result} checks')
