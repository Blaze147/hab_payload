#!/usr/bin/env python3
"""Read balloon USB logs by stable serial identity.

This read-only monitor never sends heartbeats or commands. Starting, stopping,
or losing this monitor does not control the application watchdog or RF service.
"""
import argparse
import datetime
from pathlib import Path
import sys
import time

import serial
from serial.tools import list_ports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial-number', default='5303284748B66A1C')
    parser.add_argument('--log', required=True)
    parser.add_argument('--duration', type=float, default=0,
                        help='seconds; zero keeps monitoring until interrupted')
    args = parser.parse_args()
    # picotool exits before the application USB interface finishes enumerating.
    deadline = time.monotonic() + 6
    while True:
        ports = [p for p in list_ports.comports()
                 if p.serial_number == args.serial_number]
        if ports or time.monotonic() >= deadline:
            break
        time.sleep(.1)
    if len(ports) != 1:
        print('Balloon application USB port not uniquely available. '
              'Check whether the board is in BOOTSEL.', file=sys.stderr)
        return 1
    path = Path(args.log)
    path.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    lines = 0
    pending = b''
    try:
        with serial.Serial(ports[0].device, 115200, timeout=.1,
                           write_timeout=.3, exclusive=True) as device, path.open('a') as log:
            print(f'Balloon {args.serial_number} on {device.port}; log={path}', flush=True)
            while not args.duration or time.monotonic() - started < args.duration:
                pending += device.read(4096)
                while b'\n' in pending:
                    line, pending = pending.split(b'\n', 1)
                    text = line.decode('ascii', errors='replace').rstrip('\r')
                    stamp = datetime.datetime.now(datetime.timezone.utc).isoformat()
                    log.write(f'{stamp} {text}\n')
                    lines += 1
                    if text.startswith(('SPLIT_HOST', 'RADIO_READY', 'LINK_START')):
                        print(text, flush=True)
                log.flush()
                # Bound a malformed/incomplete USB line without growing forever.
                if len(pending) > 8192:
                    log.write('MONITOR discarded overlong unterminated line\n')
                    pending = b''
    except KeyboardInterrupt:
        pass
    except (serial.SerialException, OSError) as error:
        print(f'USB monitor stopped: {error}', file=sys.stderr)
        return 2
    print(f'Monitor closed after {lines} log lines; application continues independently.', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
