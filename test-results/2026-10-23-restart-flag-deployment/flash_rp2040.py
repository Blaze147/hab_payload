"""Flash exactly one identity-checked RP2040; leave both LoRa MCUs untouched."""
import argparse
from pathlib import Path
import subprocess
import re
import time

import serial
from serial.tools import list_ports

root = Path(__file__).resolve().parent
pic = '/Users/james/.platformio/packages/tool-picotool-rp2040-earlephilhower/picotool'
identities = {'balloon': '5303284748B66A1C', 'ground': '503359276A727B9F'}
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('role', choices=identities)
args = parser.parse_args()
identity = identities[args.role]
def find_target():
    info = subprocess.run([pic, 'info', '-a'], capture_output=True, text=True, timeout=10)
    blocks = re.split(r'Device at bus (\d+), address (\d+):', info.stdout)
    if len(blocks) > 1:
        for index in range(1, len(blocks), 3):
            if identity in blocks[index + 2]:
                return ['--bus', blocks[index], '--address', blocks[index + 1]]
    elif identity in info.stdout and info.stdout.count('flash id:') == 1:
        return []
    return None

selector = find_target()
if selector is None:
    ports = [p for p in list_ports.comports() if p.serial_number == identity]
    assert len(ports) == 1, f'Expected application USB or accessible BOOTSEL for {identity}'
    print(f'{args.role}: 1200-baud touch on {ports[0].device} ({identity})', flush=True)
    with serial.Serial(ports[0].device, 1200, timeout=.1, write_timeout=1, exclusive=True) as device:
        device.dtr = False
    deadline = time.monotonic() + 8
    while selector is None and time.monotonic() < deadline:
        time.sleep(.2)
        selector = find_target()
assert selector is not None, f'{args.role}: BOOTSEL request did not expose expected flash identity {identity}'
info = subprocess.run([pic, 'info', '-a', *selector], capture_output=True, text=True, timeout=10)
(root / f'{args.role}-boot-info.log').write_text(info.stdout + info.stderr)
assert info.returncode == 0 and identity in info.stdout and info.stdout.count('flash id:') == 1, info.stdout + info.stderr
for command, name in [(['save', '-a', str(root / f'{args.role}-before.uf2')], 'backup'),
                      (['load', '-v', '-x', str(root / f'{args.role}.elf')], 'upload')]:
    result = subprocess.run([pic, *command, *selector], capture_output=True, text=True, timeout=60)
    (root / f'{args.role}-{name}.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, result.stdout + result.stderr
    print(f'{args.role}: {name} OK', flush=True)
deadline = time.monotonic() + 10
while time.monotonic() < deadline:
    ports = [p for p in list_ports.comports() if p.serial_number == identity]
    if len(ports) == 1:
        print(f'{args.role}: application USB restored at {ports[0].device}', flush=True)
        break
    time.sleep(.2)
else:
    raise RuntimeError('Application USB did not re-enumerate')
