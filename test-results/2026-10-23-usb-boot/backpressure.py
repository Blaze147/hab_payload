import subprocess,time,serial,sys,re
from serial.tools import list_ports
from pathlib import Path
pic='/Users/james/.platformio/packages/tool-picotool-rp2040-earlephilhower/picotool'
r=subprocess.run([pic,'load','-v','-x',sys.argv[1]],capture_output=True,text=True)
Path(sys.argv[2]+'.flash').write_text(r.stdout+r.stderr)
if r.returncode:sys.exit(r.returncode)
t=time.monotonic();s=None
while time.monotonic()-t<6:
 p=next((p.device for p in list_ports.comports() if p.serial_number=='5303284748B66A1C'),None)
 if p:
  try:s=serial.Serial(p,115200,timeout=.1,write_timeout=.3);break
  except serial.SerialException:pass
 time.sleep(.1)
if not s:print('NO_PORT');sys.exit(2)
f=open(sys.argv[2],'wb');last=0;was_silent=False
try:
 while time.monotonic()-t<28:
  now=time.monotonic();elapsed=now-t
  if now-last>.5:s.write(b'k');last=now
  if 5<=elapsed<20:
   if not was_silent:print('STOP_READING_AT',elapsed,flush=True);was_silent=True
   time.sleep(.05);continue
  b=s.read(4096)
  if b:f.write(b);f.flush()
  if was_silent:print('RESUMED_READ_AT',round(elapsed,3),'bytes',len(b),flush=True);was_silent=False
 print('RESET_REQUEST_AT',round(time.monotonic()-t,3),flush=True)
 mark=time.monotonic();s.baudrate=1200;s.dtr=False;s.close()
 for i in range(24):
  r=subprocess.run([pic,'info','-a'],capture_output=True,text=True)
  if r.returncode==0:print('BOOTSEL_AFTER',round(time.monotonic()-mark,3),flush=True);break
  time.sleep(.25)
 else:print('RESET_FAILED_6S',flush=True)
except Exception as e:print('ERROR',e,flush=True)
finally:s.close();f.close()
