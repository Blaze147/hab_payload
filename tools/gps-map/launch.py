#!/usr/bin/env python3
"""Start the local telemetry bridge when needed and open its dashboard."""
import json
from pathlib import Path
import subprocess
import sys
import time
from urllib.error import URLError
from urllib.request import urlopen

APP_DIR = Path(__file__).resolve().parent
URL = 'http://127.0.0.1:8765'
LOG = Path.home() / 'Library/Logs/HAB Payload/server.log'


def server_ready():
    try:
        with urlopen(URL + '/api/gps', timeout=1) as response:
            data = json.load(response)
        return isinstance(data, dict) and all(key in data for key in ('connected', 'sample', 'source'))
    except (OSError, URLError, ValueError):
        return False


def main():
    if not server_ready():
        LOG.parent.mkdir(parents=True, exist_ok=True)
        with LOG.open('ab') as log:
            process = subprocess.Popen(
                [sys.executable, '-u', str(APP_DIR / 'server.py'), '--source', 'ground',
                 '--serial-number', '503359276A727B9F'],
                cwd=APP_DIR, stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                start_new_session=True,
            )
        for _ in range(50):
            if server_ready():
                break
            if process.poll() is not None:
                # Another simultaneous launch may have won the port.
                if server_ready():
                    break
                raise RuntimeError(f'The telemetry server could not start. See {LOG}')
            time.sleep(0.2)
        else:
            raise RuntimeError(f'The telemetry server did not respond. See {LOG}')
    subprocess.run(['/usr/bin/open', URL], check=True)
    print('Dashboard opened. The telemetry server runs in the background.')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'Unable to launch dashboard: {error}', file=sys.stderr)
        sys.exit(1)
