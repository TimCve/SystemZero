#!/usr/bin/env python3
"""Boot OVMF into the i386 kernel and exercise the real UEFI handoff."""
from pathlib import Path
import re
import shutil
import struct
import tempfile
import time
import subprocess
import sys
from qemu_smoke import Guest, ROOT, COLS
from qemu_threads import wait
sys.path.insert(0,str(ROOT/'scripts'))
from uefi_vm import arguments,firmware

class UefiGuest(Guest):
    def wait_shell(self):
        def ready():
            state=self.terminal_state(); cursor=state['cursor']
            line=self.screen().splitlines()[cursor//COLS].strip()
            return self.desktop_state()['command']=='shell' and bool(re.fullmatch(r'DISK[0-3](?:p\d+)?(?:/.*)?>',line)) and cursor%COLS==len(line)+1
        wait(ready,'Command did not finish cleanup and return to the shell')

    def __init__(self,image,scratch,storage='ide',usb=False,vga='std',extra=None,reuse_variables=False):
        scratch=Path(scratch); code,variables=firmware()
        if not reuse_variables or not (scratch/'vars.fd').exists():
            shutil.copyfile(variables,scratch/'vars.fd')
        debug=scratch/'firmware.log'
        args=arguments(image,scratch/'vars.fd',storage,usb,vga,debug,True)
        if extra:
            if storage!='nvme': raise ValueError('The mixed-controller test needs an NVMe root disk')
            args+=['-drive',f'if=none,id=extra,format=raw,file={extra}',
                   '-device','ide-hd,drive=extra,bus=ide.0']
        super().__init__(image,str(scratch),launch_args=args)
        deadline=time.monotonic()+30; ready=False
        while time.monotonic()<deadline:
            log=debug.read_text() if debug.exists() else ''
            match=re.search(r'SZ UEFI env=([0-9a-f]{8})',log)
            if match and 'ExitBootServices OK' in log:
                self.env_address=int(match[1],16)
                ready=True
                break
            if self.process.poll() is not None: break
            time.sleep(.1)
        if not ready:
            if self.process.poll() is None:
                self.screenshot(ROOT/'build/uefi-failure.png')
                self.close()
            raise AssertionError(f'UEFI handoff failed: {log}')

def main(profiles=None):
    original=(ROOT/'os-image.bin').read_bytes()
    if profiles is None: profiles=(('ide',False,'std'),('ahci',True,'std'),('nvme',True,'bochs'))
    for storage,usb,vga in profiles:
        with tempfile.TemporaryDirectory(prefix=f'systemzero-uefi-{storage}-') as scratch:
            scratch=Path(scratch)
            disk=scratch/'disk.bin'; disk.write_bytes(original)
            writer=str(ROOT/'ESFS_raw_write')
            extra=scratch/'sata.bin' if storage=='nvme' else None
            if extra:
                with extra.open('wb') as output: output.truncate(8*1024*1024)
                subprocess.run([writer,'--format',str(extra)],check=True)
            subprocess.run([writer,'--delete',str(disk),'systemzero.cfg'],check=True)
            subprocess.run([writer,'--delete',str(disk),'sys/config/environment'],check=True)
            editing=scratch/'editing'; editing.write_bytes(b'abc')
            multiline=scratch/'multiline'; multiline.write_text(''.join(f'line{i:02d}\n' for i in range(100)))
            binary=scratch/'binary'; payload=bytes(range(256))*800+b'non-sector-tail'; binary.write_bytes(payload)
            for name,source in (('modern.sz',ROOT/'tests/modern_language.sz'),
                ('thread-test.sz',ROOT/'tests/thread_language.sz'),('auto.sz',ROOT/'tests/thread_auto.sz'),
                ('console-state',ROOT/'build/tests/console_state.elf'),('editing',editing),('multiline',multiline),('binary',binary)):
                subprocess.run([writer,'--replace',str(disk),name,str(source)],check=True)
            guest=UefiGuest(disk,scratch,storage,usb,vga,extra)
            try:
                guest.boot_root(timeout=30)
                boot=struct.unpack('<I',guest.memory(guest.env_address+36,4))[0]
                info=struct.unpack('<14I',guest.memory(boot,56))
                assert info[0]==0x535a5545 and info[1]==56
                assert info[2] and info[3]>=800 and info[4]>=600 and info[5]>=info[3]
                assert info[8]<=guest.heap_pointer()<info[9]<=0xc0000000
                assert info[10] and info[11] and info[12]>=40 and info[11]%info[12]==0
                # Both the loaded kernel and heap belong to firmware-reserved LoaderCode pages.
                descriptors=guest.memory(info[10],info[11])
                arena=False
                for at in range(0,len(descriptors),info[12]):
                    kind,_,base,_,pages,_=struct.unpack_from('<IIQQQQ',descriptors,at)
                    if kind==1 and base<=info[13] and info[9]<=base+pages*4096: arena=True
                assert arena,'Kernel arena was not reserved in the firmware memory map'
                runtime=struct.unpack('<I',guest.memory(guest.env_address+40,4))[0]
                assert struct.unpack('<II',guest.memory(runtime,8))[1]=={'ide':0,'ahci':1,'nvme':2}[storage]
                if extra:
                    devices=struct.unpack('<20I',guest.memory(runtime,80))
                    extra_drive=next(i for i in range(4) if devices[i*5:i*5+2]==(16384,1))
                if usb:
                    assert struct.unpack('<I',guest.memory(guest.env_address+44,4))[0],'USB keyboard was not enumerated'
                initial_heap=guest.heap_pointer()
                guest.run('drives',{'ide':'IDE0','ahci':'SATA/AHCI','nvme':'NVMe'}[storage])
                guest.run('list sys:/home/examples','hello.sz')
                guest.run('cc sys:/home/examples/hello.sz hello','Compiled'); guest.run('./hello','compiled inside SystemZero')
                guest.run('copy sys:binary sys:copied','Copied')
                if extra:
                    guest.run(f'copy sys:binary {extra_drive}:crosscopy','Copied')
                    guest.run(f'drive {extra_drive}'); guest.run('list','crosscopy')
                    # Bundled commands remain available through the system-disk fallback.
                    guest.run(f'copy {extra_drive}:crosscopy sys:roundtrip','Copied'); guest.run('drive sys')
                guest.run('cc modern.sz modern','Compiled'); guest.run('./modern','modern-storage-ok')
                guest.run('cc thread-test.sz thtest','Compiled'); guest.run('./thtest','thread-checks-ok')
                guest.run('cc auto.sz auto','Compiled'); guest.run('./auto','auto-joined')
                if usb:
                    # Exercise shifted source punctuation and modifier releases through HID.
                    guest.run('touch typed.sz')
                    guest.type('edit typed.sz'); guest.wait_for('Ctrl+S: save')
                    guest.type('int main() { print("usb-source-ok"); return 0; }',submit=False)
                    guest.key('ctrl','s'); guest.wait_for('Saved'); guest.key('esc')
                    guest.wait_shell()
                    guest.run('cc typed.sz typed','Compiled'); guest.run('./typed','usb-source-ok')
                    guest.run('touch repeat.txt')
                    guest.type('edit repeat.txt'); guest.wait_for('Ctrl+S: save')
                    guest.command('send-key',{'keys':[{'type':'qcode','data':'a'}],'hold-time':650})
                    time.sleep(.8)
                    assert guest.screen().splitlines()[0].strip().startswith('aa'),'USB key repeat failed'
                    guest.key('esc'); guest.wait_shell()
                guest.type('edit editing'); guest.wait_for('Ctrl+S: save')
                guest.key('end'); guest.type('xy',submit=False); guest.key('left'); guest.key('backspace')
                guest.key('ctrl','s'); guest.wait_for('Saved'); guest.key('esc'); guest.wait_shell()
                guest.type('hexedit editing'); guest.wait_for('HEX editing'); guest.key('c')
                guest.type('41'); guest.key('w'); guest.wait_for('Saved'); guest.key('esc'); guest.wait_shell()
                guest.type('./console-state')
                for title in ('Ctrl+S: save','HEX editing','line00'):
                    guest.wait_for(title); guest.key('esc')
                guest.wait_for('console-restore-ok'); guest.wait_for('DISK0p2>')
                guest.wait_shell()
                assert guest.heap_pointer()==initial_heap,(storage,'heap leak',hex(initial_heap),hex(guest.heap_pointer()))
                guest.type('settings'); guest.wait_for('SystemZero Settings')
                guest.key('tab'); guest.key('shift','tab')
                guest.type('12',submit=False); guest.key('tab'); guest.type('34',submit=False)
                guest.key('tab'); guest.type('56',submit=False); guest.key('tab'); guest.key('right')
                guest.key('ret'); wait(lambda: guest.desktop_state()['position']==1,'Settings were not applied')
                guest.wait_for('DISK0p2>'); guest.screenshot(ROOT/f'build/uefi-{storage}.png')
            finally: guest.close()
            # Verify write/flush durability and exact binary data outside the guest.
            extracted=scratch/'extracted'
            for name,expected in (('copied',payload),('editing',b'Abcy'),('modern-output',bytes(i%127 for i in range(131583)))):
                subprocess.run([writer,'--extract',str(disk),name,str(extracted)],check=True)
                assert extracted.read_bytes()==expected,(storage,name)
            subprocess.run([writer,'--check',str(disk)],check=True)
            if extra:
                subprocess.run([writer,'--extract',str(extra),'crosscopy',str(extracted)],check=True)
                assert extracted.read_bytes()==payload
                subprocess.run([writer,'--extract',str(disk),'roundtrip',str(extracted)],check=True)
                assert extracted.read_bytes()==payload
                subprocess.run([writer,'--check',str(extra)],check=True)
            guest=UefiGuest(disk,scratch,storage,usb,vga,extra)
            try:
                guest.boot_root(); assert guest.desktop_state()['position']==1
                assert (guest.desktop_state()['seconds']-(12*3600+34*60+56))%86400<20
                guest.run('./modern','modern-storage-ok')
            finally: guest.close()
            print(f'PASS UEFI {storage}: graphics, keyboard, storage, editors, compiler, threads, persistence',flush=True)
    assert (ROOT/'os-image.bin').read_bytes()==original,'UEFI tests modified the user disk'
    print(f'All {len(profiles)} UEFI profiles passed; original disk was untouched.',flush=True)

if __name__=='__main__': main()
