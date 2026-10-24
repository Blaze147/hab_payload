import sys,time,subprocess,serial
from serial.tools import list_ports
from pathlib import Path
image,duration,log=sys.argv[1],float(sys.argv[2]),sys.argv[3]
picotool='/Users/james/.platformio/packages/tool-picotool-rp2040-earlephilhower/picotool'
r=subprocess.run([picotool,'load','-v','-x',image],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
Path(log+'.flash').write_text(r.stdout)
if r.returncode: print(r.stdout);sys.exit(r.returncode)
t=time.monotonic();dev=None
while time.monotonic()-t<6:
 ports=list(list_ports.comports())
 print([(p.device,p.serial_number) for p in ports],flush=True)
 p=next((p.device for p in ports if p.serial_number=='5303284748B66A1C'),None)
 if p:
  try:dev=serial.Serial(p,115200,timeout=.1,write_timeout=.3);break
  except serial.SerialException:pass
 time.sleep(.1)
if not dev:print('No serial port before recovery deadline');sys.exit(2)
print('Opened',dev.port,flush=True)
f=open(log,'w');last=0;buf=b'';heartbeats=0;last_reply=time.monotonic()
try:
 while time.monotonic()-t<duration:
  now=time.monotonic()
  if now-last>.5:dev.write(b'k');last=now
  b=dev.read(4096)
  if time.monotonic()-last_reply>4:
   print('USB_HEARTBEAT_LOST_AT',round(time.monotonic()-t,3),flush=True);break
  if not b:continue
  buf+=b
  while b'\n' in buf:
   line,buf=buf.split(b'\n',1);line=line.decode(errors='replace')
   f.write(f'{now-t:.3f} {line}\n');f.flush()
   if 'USB_HEARTBEAT' in line:heartbeats+=1;last_reply=time.monotonic()
   if 'SPLIT_HOST' in line:print(f'{now-t:.1f} {line}',flush=True)
 print('Finished; USB heartbeats:',heartbeats,flush=True)
 if len(sys.argv)>4 and sys.argv[4]=='reset':
  reset_at=time.monotonic()
  dev.baudrate=1200;dev.dtr=False;dev.close()
  for attempt in range(28):
   r=subprocess.run([picotool,'info','-a'],capture_output=True,text=True)
   if r.returncode==0 and '5303284748B66A1C' in r.stdout:
    print('SOFTWARE_RESET_BOOTSEL_AFTER',round(time.monotonic()-reset_at,3),flush=True);break
   time.sleep(.25)
  else:print('SOFTWARE_RESET_NOT_SEEN_WITHIN_7_SECONDS',flush=True)
except (serial.SerialException,OSError) as e:print('Serial error:',e,flush=True)
finally:dev.close();f.close()
# Deliberately stop heartbeats: this should return the test image to BOOTSEL.
