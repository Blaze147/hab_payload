import serial,time,sys,subprocess
from serial.tools import list_ports
out=open(sys.argv[1],'w');t=time.monotonic()
def log(s):
 print(s,flush=True);out.write(f'{time.monotonic()-t:.3f} {s}\n');out.flush()
for i in range(30):
 p=next((p.device for p in list_ports.comports() if p.serial_number=='5303284748B66A1C'),None)
 if not p:log(f'FAIL cycle={i} no_application_port');break
 s=serial.Serial(p,115200,timeout=.1,write_timeout=.3)
 start=time.monotonic();last=0;buf=b'';ack=False
 try:
  while time.monotonic()-start<1.2:
   now=time.monotonic()
   if now-last>.3:s.write(b'k');last=now
   buf+=s.read(2048)
   if b'USB_HEARTBEAT' in buf:ack=True
  log(f'REOPEN cycle={i} ack={int(ack)} bytes={len(buf)}')
  if not ack:break
  if i==29:
   mark=time.monotonic();s.baudrate=1200;s.dtr=False;s.close()
   while time.monotonic()-mark<6:
    r=subprocess.run(['/Users/james/.platformio/packages/tool-picotool-rp2040-earlephilhower/picotool','info','-a'],capture_output=True,text=True)
    if r.returncode==0:log(f'RESET_BOOTSEL after_s={time.monotonic()-mark:.3f}');break
    time.sleep(.2)
   else:log('FAIL software_reset_not_seen_in_6s')
 except Exception as e:log(f'FAIL cycle={i} {e}');break
 finally:s.close()
 time.sleep(.15)
out.close()
