#!/usr/bin/env python3
"""Boot both firmware paths after moving the system volume to another GPT slot."""
from pathlib import Path
import subprocess
import tempfile
import sys
from qemu_smoke import Guest, ROOT
from qemu_uefi import UefiGuest
sys.path.insert(0, str(ROOT / 'scripts'))
from disk_image import geometry, write_gpt, EFI_GUID, ESFS_GUID

def main():
    original = (ROOT / 'os-image.bin').read_bytes()
    with tempfile.TemporaryDirectory(prefix='systemzero-partition-boot-') as temp:
        scratch=Path(temp); disk=scratch/'disk.bin'; disk.write_bytes(original)
        writer=ROOT/'ESFS_raw_write'; info=geometry(writer,disk)
        efi=next(p for p in info['partitions'] if p['guid']==EFI_GUID.hex())
        with disk.open('r+b') as output:
            boot=output.read(512)
            write_gpt(output, [(1,EFI_GUID,efi['first'],efi['sectors'],'SystemZero boot'),
                (3,ESFS_GUID,info['first'],info['sectors'],'SystemZero system')], info['disk_sectors'],boot)
        subprocess.run([str(writer),'--volume','3','--check',str(disk)],check=True)
        for uefi in (False,True):
            guest=UefiGuest(disk,scratch,'nvme',True) if uefi else Guest(disk,temp)
            try:
                guest.wait_for('DISK0p3/home>',timeout=30)
                assert guest.memory(guest.env_address+48,2)==b'\x03\x03'
                guest.run('pwd','0p3:/home')
                guest.run('set SYSTEM_VOLUME','SYSTEM_VOLUME=0p3:')
                guest.run('set PATH','PATH=sys:/bin')
                guest.run('cc examples/sdk.sz sdk io.o','Compiled')
                guest.run('./sdk','Hello from a linked SystemZero library!')
            except BaseException:
                print(guest.screen()); raise
            finally:
                guest.close()
        subprocess.run([str(writer),'--check',str(disk)],check=True)
    assert (ROOT/'os-image.bin').read_bytes()==original
    print('PASS single disk, alternate GPT slot: BIOS/UEFI discovery, environment paths, SDK compilation',flush=True)

if __name__=='__main__': main()
