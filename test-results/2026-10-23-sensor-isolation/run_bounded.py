"""Run the fixed-deadline RF image; keep ground capture after balloon USB loss."""
import subprocess,time,sys,serial
from pathlib import Path
from serial.tools import list_ports
picotool='/Users/james/.platformio/packages/tool-picotool-rp2040-earlephilhower/picotool'
identity='5303284748B66A1C'
image,stem=sys.argv[1:]
ground=Path('test-results/2026-10-23-ground-message-deployment/dashboard-serial.log')
offset=ground.stat().st_size
check=subprocess.run([picotool,'info','-a'],capture_output=True,text=True)
assert check.returncode==0 and identity in check.stdout,check.stdout+check.stderr
subprocess.run([picotool,'load','-v','-x',image],check=True)
start=time.monotonic();dev=None;last_write=0;last_reply=start;buf=b''
with open(stem+'.log','w') as out:
 while time.monotonic()-start<198:
  elapsed=time.monotonic()-start
  if dev is None and elapsed<5:
   p=next((p.device for p in list_ports.comports() if p.serial_number==identity),None)
   if p:
    try:dev=serial.Serial(p,115200,timeout=.1,write_timeout=.3,exclusive=True)
    except serial.SerialException:pass
  if dev:
   try:
    if elapsed<175 and time.monotonic()-last_write>.5:
     dev.write(b'k');last_write=time.monotonic()
    buf+=dev.read(4096)
    while b'\n' in buf:
     line,buf=buf.split(b'\n',1);line=line.decode(errors='replace')
     out.write(f'{elapsed:.3f} {line}\n');out.flush()
     if 'USB_HEARTBEAT' in line:last_reply=time.monotonic()
     if 'SPLIT_HOST' in line:print(f'{elapsed:.1f} {line}',flush=True)
    if time.monotonic()-last_reply>4:
     print(f'USB replies lost at {elapsed:.1f}; bounded RF run continues until watchdog deadline',flush=True)
     dev.close();dev=None
   except (OSError,serial.SerialException) as e:
    print(e,flush=True);dev.close();dev=None
  else:time.sleep(.2)
  if elapsed>185:
   check=subprocess.run([picotool,'info','-a'],capture_output=True,text=True)
   if check.returncode==0 and identity in check.stdout:
    print(f'BOOTSEL recovery confirmed at {elapsed:.1f} seconds',flush=True);break
 if dev:dev.close()
with ground.open('rb') as source:
 source.seek(offset);Path(stem+'-ground.log').write_bytes(source.read())
