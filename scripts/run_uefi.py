#!/usr/bin/env python3
import argparse
from pathlib import Path
import subprocess
import sys
from uefi_vm import arguments,ROOT

def main():
    parser=argparse.ArgumentParser(description='Run SystemZero with OVMF UEFI firmware')
    parser.add_argument('--storage',choices=('ide','ahci','nvme'),default='nvme')
    parser.add_argument('--ps2',action='store_true',help='Use legacy keyboard input instead of USB')
    parser.add_argument('--display',choices=('std','bochs'),default='std')
    args=parser.parse_args()
    variables=ROOT/'build/uefi'/f'vars-{args.storage}.fd'
    command=arguments(ROOT/'os-image.bin',variables,
        args.storage,not args.ps2,args.display,ROOT/'build/uefi/firmware.log')
    return subprocess.call(command,cwd=ROOT)

if __name__=='__main__':
    try: sys.exit(main())
    except (OSError,RuntimeError) as error: sys.exit(str(error))
