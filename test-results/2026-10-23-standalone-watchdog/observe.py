"""Validate normal full-sensor firmware without any PC heartbeats or fault commands."""
import subprocess,time,serial,json
from pathlib import Path
from serial.tools import list_ports
root=Path('test-results/2026-10-23-standalone-watchdog')
pic='/Users/james/.platformio/packages/tool-picotool-rp2040-earlephilhower/picotool'
ground=Path('test-results/2026-10-23-ground-message-deployment/dashboard-serial.log')
offset=ground.stat().st_size
info=subprocess.run([pic,'info','-a'],capture_output=True,text=True)
assert info.returncode==0 and '5303284748B66A1C' in info.stdout and info.stdout.count('flash id:')==1,info.stdout
loaded=subprocess.run([pic,'load','-v','-x',str(root/'balloon.elf')],capture_output=True,text=True)
(root/'upload.log').write_text(loaded.stdout+loaded.stderr);assert loaded.returncode==0
start=time.monotonic();device=None
while time.monotonic()-start<7:
 p=next((p.device for p in list_ports.comports() if p.serial_number=='5303284748B66A1C'),None)
 if p:
  try:device=serial.Serial(p,115200,timeout=.1,exclusive=True);break
  except serial.SerialException:pass
 time.sleep(.1)
assert device is not None,'Balloon did not enumerate'
print('Opened read-only capture:',device.port,'No command bytes are sent.',flush=True)
buf=b''
with (root/'startup.log').open('w') as log:
 while time.monotonic()-start<15:
  buf+=device.read(4096)
  while b'\n' in buf:
   line,buf=buf.split(b'\n',1);text=line.decode(errors='replace')
   log.write(f'{time.monotonic()-start:.3f} {text}\n');log.flush()
 device.close()
 print('Closed USB serial at 15 seconds. Continuing with NO balloon monitor.',flush=True)
 for target in [60,120,180]:
  while time.monotonic()-start<target:time.sleep(.2)
  info=subprocess.run([pic,'info','-a'],capture_output=True,text=True)
  is_bootsel=info.returncode==0 and '5303284748B66A1C' in info.stdout
  print(f'{target}s: balloon in BOOTSEL={is_bootsel}',flush=True)
  assert not is_bootsel,'Unexpected watchdog recovery while running independently'
with ground.open('rb') as f:
 f.seek(offset);(root/'ground.log').write_bytes(f.read())
(root/'result.json').write_text(json.dumps({'duration_seconds':180,'heartbeat_commands_sent':0,'fault_commands_sent':0,'monitor_closed_at_seconds':15,'unexpected_bootsel':False},indent=2)+'\n')
print('PASS: 180 seconds without PC heartbeats; USB monitor closed after 15 seconds.',flush=True)
