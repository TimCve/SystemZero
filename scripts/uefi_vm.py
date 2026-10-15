"""Shared QEMU UEFI profiles for interactive runs and headless regression tests."""
import os
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]

def firmware():
    directory=Path(os.environ.get('OVMF_DIR','/usr/share/OVMF'))
    code=directory/'OVMF_CODE_4M.fd'; variables=directory/'OVMF_VARS_4M.fd'
    if not code.exists() or not variables.exists():
        raise RuntimeError('Install ovmf or set OVMF_DIR to its firmware directory')
    return code,variables

def arguments(image,variables,storage='nvme',usb=True,display='std',debug=None,headless=False):
    code,template=firmware()
    variables=Path(variables)
    if not variables.exists(): shutil.copyfile(template,variables)
    machine=('pc' if storage=='ide' else 'q35')+(',i8042=off' if usb else '')
    args=['qemu-system-x86_64','-accel','tcg','-machine',machine,'-m','256M',
        '-vga','none' if display=='bochs' else display,
        '-drive',f'if=pflash,format=raw,unit=0,readonly=on,file={code}',
        '-drive',f'if=pflash,format=raw,unit=1,file={variables}',
        '-drive',f'if=none,id=root,format=raw,file={image}']
    if storage=='ide':
        args+=['-device','ide-hd,drive=root,bus=ide.0,unit=0,bootindex=1']
    else:
        if storage=='ahci': args+=['-device','ide-hd,drive=root,bus=ide.0,bootindex=1']
        elif storage=='nvme': args+=['-device','nvme,drive=root,serial=SYSTEMZERO,bootindex=1']
        else: raise ValueError(storage)
    if usb: args+=['-device','qemu-xhci,id=usb','-device','usb-kbd,bus=usb.0','-device','usb-mouse,bus=usb.0']
    if display=='bochs': args+=['-device','bochs-display']
    if debug: args+=['-debugcon',f'file:{debug}']
    if headless: args+=['-display','none','-serial','none','-monitor','none','-no-reboot']
    return args
