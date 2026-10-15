#!/usr/bin/env python3
"""Build a UEFI El Torito ISO with a GPT-mapped FAT image for USB boot."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib
from build_uefi import fat32
from disk_image import EFI_GUID, write_gpt

ROOT = Path(__file__).resolve().parents[1]

def manifest(image, destination):
    data = Path(image).read_bytes()
    if len(data) % 512 or len(data) > 0xffffffff:
        raise ValueError('installer payload must fit a 32-bit byte offset and be sector aligned')
    Path(destination).write_text(f'#define INSTALL_IMAGE_BYTES {len(data)}u\n#define INSTALL_IMAGE_CRC 0x{zlib.crc32(data):08x}u\n')

def hybridize(path, fat_bytes):
    """Expose the El Torito FAT extent as a real GPT EFI system partition.

    GPT occupies the ISO system area; its backup follows the ISO volume. No
    partition overlaps another, and the exact same FAT is booted from DVD/USB.
    """
    path = Path(path)
    with path.open('r+b') as image:
        catalog = None
        for lba in range(16,32):
            image.seek(lba*2048); descriptor=image.read(2048)
            if descriptor[1:6] != b'CD001':
                raise ValueError('invalid ISO volume descriptor')
            if descriptor[0] == 0 and descriptor[7:30] == b'EL TORITO SPECIFICATION':
                catalog=struct.unpack_from('<I',descriptor,71)[0]
                break
            if descriptor[0] == 255: break
        if catalog is None: raise ValueError('ISO has no El Torito boot record')
        image.seek(catalog*2048); entries=image.read(64)
        if entries[0:2] != b'\x01\xef' or entries[30:32] != b'\x55\xaa' or entries[32:34] != b'\x88\0':
            raise ValueError('ISO has no bootable, non-emulated x64 EFI entry')
        first=struct.unpack_from('<I',entries,40)[0]*4
        length=fat_bytes//512
        original=path.stat().st_size
        if fat_bytes%512 or first<64 or (first+length)*512>original:
            raise ValueError('EFI boot extent is outside the ISO')
        sectors=(original+33*512+2047)//2048*4
        image.truncate(sectors*512)
        write_gpt(image,[(1,EFI_GUID,first,length,'SystemZero installer')],sectors,bytes(512))
    return sectors*512

def build(loader, payload, destination, tool=None):
    tool=tool or shutil.which('genisoimage') or shutil.which('mkisofs')
    if not tool:
        raise RuntimeError('Install genisoimage to build installer media')
    destination=Path(destination).resolve()
    destination.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.systemzero-installer-',dir=destination.parent) as scratch:
        scratch=Path(scratch); tree=scratch/'iso'; boot=tree/'boot'; boot.mkdir(parents=True)
        # Fresh payload is independent of os-image.bin and its user files.
        content=Path(payload).read_bytes()
        fat_sectors=((len(content)+32*1024*1024+1024*1024-1)//(1024*1024)) * 2048
        esp=boot/'efi.img'
        esp.write_bytes(fat32(Path(loader).read_bytes(),fat_sectors,0,{b'SYSTEM  IMG':content}))
        (tree/'README.TXT').write_text(
            'SystemZero UEFI installer\n\n'
            'Boot this ISO as a DVD or write it directly to a USB drive as a raw image.\n'
            'Requires x64 UEFI with Secure Boot disabled. Installation replaces all\n'
            'partitions on the chosen destination disk. Use a spare disk and backups.\n'
            'The installer USB is protected. The installed kernel needs supported\n'
            'AHCI/NVMe/IDE storage, GOP graphics, and PS/2 or direct xHCI input.\n'
            'See docs/install.md in the source tree for requirements and limitations.\n')
        output=scratch/'installer.iso'
        # UEFI 13.3.2.1 defines counts 0/1 as extending to the medium's end;
        # the FAT image is too large for the catalog's 16-bit sector count.
        subprocess.run([str(tool),'-quiet','-V','SYSTEMZERO_INSTALL','-e','boot/efi.img',
            '-no-emul-boot','-boot-load-size','1','-c','boot/boot.cat','-o',str(output),str(tree)],check=True)
        size=hybridize(output,esp.stat().st_size)
        output.replace(destination)
    print(f'Ready: {destination} ({size//1024//1024} MiB; UEFI DVD/USB installer)')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest',action='store_true')
    parser.add_argument('paths',nargs='+')
    args=parser.parse_args()
    if args.manifest:
        if len(args.paths)!=2: parser.error('--manifest requires IMAGE HEADER')
        manifest(*args.paths)
    else:
        if len(args.paths)!=3: parser.error('requires INSTALLER.EFI SYSTEM.IMG OUTPUT.ISO')
        build(*args.paths)

if __name__=='__main__': main()
