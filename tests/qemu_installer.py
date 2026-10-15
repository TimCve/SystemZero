#!/usr/bin/env python3
"""Install from DVD and USB onto disposable AHCI/NVMe disks, then cold boot."""
import hashlib
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from qemu_smoke import Guest,ROOT
from qemu_uefi import UefiGuest
sys.path.insert(0,str(ROOT/'scripts'))
from run_installer import arguments

def wait_trace(guest,path,message,timeout=90):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        text=path.read_text() if path.exists() else ''
        if message in text: return text
        if guest.process.poll() is not None: break
        time.sleep(.1)
    guest.screenshot(ROOT/'build/installer-failure.png')
    raise AssertionError(f'Installer did not reach {message!r}: {text}')

def sha(path):
    with Path(path).open('rb') as source: return hashlib.file_digest(source,'sha256').digest()

def multiple_destinations(iso):
    with tempfile.TemporaryDirectory(prefix='sz-install-multiple-') as temp:
        scratch=Path(temp); disks=[scratch/'first.img',scratch/'second.img']
        for path,size in zip(disks,(512,384)):
            with path.open('wb') as out: out.truncate(size*1024*1024)
        before=[sha(path) for path in disks]; trace=scratch/'install.log'
        args=arguments(iso,disks[0],scratch/'vars.fd','cdrom','nvme',trace,True)
        args+=['-drive',f'if=none,id=second,format=raw,file={disks[1]}',
            '-device','nvme,drive=second,serial=SECOND_DISK']
        for phrase,success in (('ERASE DISK 1',False),('ERASE DISK 2',True)):
            trace.unlink(missing_ok=True)
            guest=Guest(disks[0],temp,launch_args=args)
            try:
                wait_trace(guest,trace,'Eligible destination disks: 2')
                guest.key('right'); guest.key('ret')
                log=wait_trace(guest,trace,'Type ERASE DISK 2')
                selected=re.findall(r'Disk 2  (\d+) MiB',log)
                assert selected,log
                chosen=(512,384).index(int(selected[-1]))
                guest.type(phrase,submit=True)
                wait_trace(guest,trace,'SZ INSTALL success' if success else 'SZ INSTALL cancelled')
            finally: guest.close()
            if not success:
                assert [sha(path) for path in disks]==before,'A wrong disk number bypassed confirmation'
            else:
                assert sha(disks[1-chosen])==before[1-chosen],'The unselected disk changed'
                assert sha(disks[chosen])!=before[chosen],'The selected disk was not installed'
                subprocess.run([str(ROOT/'ESFS_raw_write'),'--check',str(disks[chosen])],check=True)
        print('PASS installer multiple disks: navigation, exact confirmation, unselected disk preservation',flush=True)

def main():
    iso=ROOT/'SystemZero-install.iso'; original=sha(iso)
    for media,storage in (('cdrom','ahci'),('usb','nvme')):
        with tempfile.TemporaryDirectory(prefix=f'sz-install-{media}-') as temp:
            scratch=Path(temp); disk=scratch/'target.img'; source=scratch/'install.iso'
            shutil.copyfile(iso,source)
            # A flashed USB stick is usually larger than the ISO itself.
            if media=='usb':
                with source.open('r+b') as out: out.truncate(768*1024*1024)
            source_before=sha(source)
            with disk.open('wb') as out: out.truncate(512*1024*1024)
            before=sha(disk); trace=scratch/'install.log'
            args=arguments(source,disk,scratch/'vars.fd',media,storage,trace,True)
            guest=Guest(disk,temp,launch_args=args)
            try:
                wait_trace(guest,trace,'SZ INSTALL ready')
                guest.key('ret'); guest.type('NO',submit=True)
                wait_trace(guest,trace,'SZ INSTALL cancelled')
            finally: guest.close()
            assert sha(disk)==before,'Cancellation changed the destination'
            assert sha(source)==source_before,'Cancellation changed installer media'
            trace.unlink()
            guest=Guest(disk,temp,launch_args=args)
            try:
                wait_trace(guest,trace,'SZ INSTALL ready')
                guest.screenshot(ROOT/f'build/installer-{media}-select.png')
                guest.key('ret'); guest.type('ERASE DISK 1',submit=True)
                log=wait_trace(guest,trace,'SZ INSTALL success')
                assert 'SZ INSTALL boot entry saved' in log,'Firmware boot entry was not created'
                guest.screenshot(ROOT/f'build/installer-{media}-success.png')
            finally: guest.close()
            assert sha(source)==source_before,'Installer overwrote its own media'
            writer=str(ROOT/'ESFS_raw_write')
            subprocess.run([writer,'--check',str(disk)],check=True)
            # Partition scan requires a correct backup at the physical disk end.
            info=__import__('json').loads(subprocess.check_output([writer,'--geometry',str(disk)]))
            assert info['table']==2 and not info['readonly'] and info['disk_sectors']==512*2048,info
            guest=UefiGuest(disk,scratch,storage,True,reuse_variables=True)
            try:
                guest.boot_root(timeout=40)
                guest.run('list sys:/bin','shell')
                guest.run('cc home/examples/hello.sz hello','Compiled')
                guest.run('./hello','Hello')
                guest.run('set INSTALL_TEST yes')
            finally: guest.close()
            guest=UefiGuest(disk,scratch,storage,True,reuse_variables=True)
            try:
                guest.boot_root(timeout=40)
                guest.run('set INSTALL_TEST','INSTALL_TEST=yes')
            finally: guest.close()
            print(f'PASS installer {media} -> {storage}: cancel, source protection, GPT, cold boot, compiler, persistence',flush=True)
    multiple_destinations(iso)
    assert sha(iso)==original

if __name__=='__main__': main()
