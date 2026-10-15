#!/usr/bin/env python3
"""Run installer media against a regular, disposable virtual disk."""
import argparse
from pathlib import Path
import stat
import subprocess
from uefi_vm import ROOT,firmware

def arguments(iso,disk,variables,media='cdrom',storage='nvme',debug=None,headless=False):
    import shutil
    code,template=firmware()
    variables=Path(variables)
    if not variables.exists(): shutil.copyfile(template,variables)
    args=['qemu-system-x86_64','-accel','tcg','-machine','q35,i8042=off','-m','512M','-vga','std',
        '-drive',f'if=pflash,format=raw,unit=0,readonly=on,file={code}',
        '-drive',f'if=pflash,format=raw,unit=1,file={variables}',
        '-drive',f'if=none,id=target,format=raw,file={disk}']
    if storage=='nvme': args+=['-device','nvme,drive=target,serial=INSTALL_TARGET,bootindex=2']
    elif storage=='ahci': args+=['-device','ide-hd,drive=target,bus=ide.0,bootindex=2']
    else: raise ValueError(storage)
    args+=['-device','qemu-xhci,id=usb','-device','usb-kbd,bus=usb.0','-device','usb-mouse,bus=usb.0']
    if media=='cdrom':
        args+=['-drive',f'if=none,id=installer,format=raw,media=cdrom,readonly=on,file={iso}',
            '-device','ide-cd,drive=installer,bus=ide.1,bootindex=1']
    elif media=='usb':
        args+=['-drive',f'if=none,id=installer,format=raw,file={iso}',
            '-device','usb-storage,drive=installer,bus=usb.0,removable=on,bootindex=1']
    else: raise ValueError(media)
    if debug: args+=['-debugcon',f'file:{debug}']
    if headless: args+=['-display','none','-serial','none','-monitor','none','-no-reboot']
    return args

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--usb',action='store_true',help='Test the ISO as raw USB media')
    parser.add_argument('--storage',choices=('nvme','ahci'),default='nvme')
    options=parser.parse_args()
    directory=ROOT/'build/installer'; directory.mkdir(parents=True,exist_ok=True)
    disk=directory/'test-disk.img'
    # Never accept a host device or replace an existing image.
    if not disk.exists():
        with disk.open('xb') as output: output.truncate(1024*1024*1024)
    if not stat.S_ISREG(disk.lstat().st_mode): raise RuntimeError(f'{disk} must be a regular file')
    print(f'Installer destination is the virtual disk {disk}',flush=True)
    return subprocess.call(arguments(ROOT/'SystemZero-install.iso',disk,directory/'installer-vars.fd',
        'usb' if options.usb else 'cdrom',options.storage,directory/'installer.log'),cwd=ROOT)

if __name__=='__main__': raise SystemExit(main())
