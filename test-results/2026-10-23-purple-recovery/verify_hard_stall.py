import subprocess,time,serial,struct,re
from pathlib import Path
from serial.tools import list_ports
root=Path('test-results/2026-10-23-purple-recovery')
pic='/Users/james/.platformio/packages/tool-picotool-rp2040-earlephilhower/picotool'
image=root/'balloon-purple.elf'
info=subprocess.run([pic,'info','-a'],capture_output=True,text=True)
assert info.returncode==0 and 'flash id:    0x5303284748B66A1C' in info.stdout,info.stdout
r=subprocess.run([pic,'load','-v','-x',str(image)],capture_output=True,text=True)
(root/'hard-stall-flash.log').write_text(r.stdout+r.stderr);assert r.returncode==0
started=time.monotonic();s=None
while time.monotonic()-started<6:
 p=next((p.device for p in list_ports.comports() if p.serial_number=='5303284748B66A1C'),None)
 if p:
  try:s=serial.Serial(p,115200,timeout=.1,write_timeout=.3);break
  except serial.SerialException:pass
 time.sleep(.1)
assert s is not None
last=0;received=b''
while time.monotonic()-started<4:
 if time.monotonic()-last>.5:s.write(b'k');last=time.monotonic()
 received+=s.read(4096)
assert b'USB_HEARTBEAT' in received
s.write(b'h');s.flush();halt=time.monotonic();s.close()
print('Sent diagnostic hard stall: core 0 disables interrupts and loops forever.',flush=True)
while time.monotonic()-halt<12:
 r=subprocess.run([pic,'info','-a'],capture_output=True,text=True)
 if r.returncode==0 and '5303284748B66A1C' in r.stdout:break
 time.sleep(.2)
else:raise RuntimeError('No BOOTSEL recovery within 12 seconds')
elapsed=time.monotonic()-halt
nm=subprocess.check_output(['/Users/james/.platformio/packages/toolchain-rp2040-earlephilhower/bin/arm-none-eabi-nm','-C',str(image)],text=True)
addr=int(next(l.split()[0] for l in nm.splitlines() if 'usb_recovery_diagnostic::trace' in l),16)
r=subprocess.run([pic,'save','-r',hex(addr),hex(addr+96),str(root/'hard-stall-trace.bin')],capture_output=True,text=True);assert r.returncode==0,r.stdout
trace=struct.unpack('<24I',(root/'hard-stall-trace.bin').read_bytes())
assert trace[4]==9 and trace[7]==0x48414c54 and trace[8]==1,trace
text=f'PASS: forced CPU/interrupt stall returned to balloon BOOTSEL in {elapsed:.3f}s without buttons; purple indicator hook completed.\ntrace={trace}\n'
(root/'hard-stall-result.log').write_text(text);print(text,flush=True)
