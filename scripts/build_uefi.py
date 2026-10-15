#!/usr/bin/env python3
"""Create the FAT32 EFI partition payload for the shared BIOS/UEFI disk."""
from pathlib import Path
import struct
import sys

def fat32(loader, volume_sectors=131072, first=2048, files=None):
    sector = 512
    image = bytearray(volume_sectors * sector)
    fats, reserved, fat_sectors = 2, 32, 1
    while True:
        clusters = volume_sectors - reserved - fats*fat_sectors
        required = (clusters+2)*4//sector+1
        if required == fat_sectors: break
        fat_sectors = required
    base = 0
    boot = bytearray(sector)
    boot[:11] = b'\xeb\x58\x90SYSTEMZR'
    struct.pack_into('<HBHBHHBHHHII', boot, 11, sector, 1, reserved, fats, 0, 0, 0xf8, 0, 63, 255, first, volume_sectors)
    struct.pack_into('<IHHIHH', boot, 36, fat_sectors, 0, 0, 2, 1, 6)
    struct.pack_into('<BBB', boot, 64, 0x80, 0, 0x29)
    struct.pack_into('<I', boot, 67, 0x535a5545)
    boot[71:90] = b'SYSTEMZERO FAT32   '
    boot[510:512] = b'\x55\xaa'
    image[base:base+sector] = boot
    image[base+6*sector:base+7*sector] = boot
    fsinfo = bytearray(sector)
    struct.pack_into('<I',fsinfo,0,0x41615252)
    struct.pack_into('<III',fsinfo,484,0x61417272,0xffffffff,0xffffffff)
    struct.pack_into('<I',fsinfo,508,0xaa550000)
    image[base+sector:base+2*sector] = fsinfo
    image[base+7*sector:base+8*sector] = fsinfo
    fat = bytearray(fat_sectors*sector)
    for index,value in enumerate((0xffffff8,0xfffffff,0xfffffff,0xfffffff,0xfffffff)):
        struct.pack_into('<I',fat,index*4,value)
    file_clusters = (len(loader)+sector-1)//sector
    if not file_clusters or file_clusters+3 > clusters: raise ValueError('EFI application too large')
    contents = [(b'BOOTX64 EFI', loader, 5)]
    next_cluster = 5 + file_clusters
    for name, content in (files or {}).items():
        if len(name) != 11 or len(contents) >= 16 or not content:
            raise ValueError('extra FAT files require nonempty data and an 11-byte 8.3 name')
        count = (len(content) + sector - 1) // sector
        if next_cluster + count > clusters + 2:
            raise ValueError('EFI volume is too small for installer files')
        contents.append((name, content, next_cluster))
        next_cluster += count
    for _, content, cluster in contents:
        count = (len(content) + sector - 1) // sector
        for index in range(count):
            struct.pack_into('<I',fat,(cluster+index)*4,0xfffffff if index+1 == count else cluster+index+1)
    for index in range(fats):
        start = base+(reserved+index*fat_sectors)*sector
        image[start:start+len(fat)] = fat
    data = base+(reserved+fats*fat_sectors)*sector
    def entry(name, attributes, cluster, size=0):
        result = bytearray(32)
        result[:11] = name
        result[11] = attributes
        struct.pack_into('<H',result,20,cluster>>16)
        struct.pack_into('<HI',result,26,cluster&65535,size)
        return result
    image[data:data+32] = entry(b'EFI        ',0x10,3)
    image[data+sector:data+sector+96] = b''.join((entry(b'.          ',0x10,3),entry(b'..         ',0x10,0),entry(b'BOOT       ',0x10,4)))
    image[data+2*sector:data+2*sector+96] = b''.join((entry(b'.          ',0x10,4),entry(b'..         ',0x10,3),entry(b'BOOTX64 EFI',0x20,5,len(loader))))
    for index, (name, content, cluster) in enumerate(contents):
        if index:
            image[data+index*32:data+(index+1)*32] = entry(name,0x20,cluster,len(content))
        start = data + (cluster-2)*sector
        image[start:start+len(content)] = content
    return image

def build(loader, destination):
    image = fat32(loader)
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix('.tmp')
    temporary.write_bytes(image)
    temporary.replace(destination)
    print(f"Ready: {destination} (FAT32 partition payload, EFI/BOOT/BOOTX64.EFI)")

if __name__ == '__main__':
    build(Path(sys.argv[1]).read_bytes(), sys.argv[2])
