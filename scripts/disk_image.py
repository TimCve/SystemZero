"""Partition layout and boot updates for the single SystemZero disk image."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import uuid
import zlib

SECTOR = 512
ALIGNMENT = 2048
EFI_FIRST = ALIGNMENT
EFI_SECTORS = 131072
SYSTEM_FIRST = EFI_FIRST + EFI_SECTORS
EFI_GUID = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le
ESFS_GUID = bytes.fromhex('535a46530e13424d947a7379737a6572')
NAMESPACE = uuid.UUID('6b297c9c-498c-42df-a674-85812fc31d48')
BOOT_MARKER = b'SZLBA32\0'

def geometry(writer, image):
    return json.loads(subprocess.check_output([str(writer), '--geometry', str(image)], text=True))

def boot_sector(source, first):
    boot = bytearray(source)
    at = boot.find(BOOT_MARKER)
    if len(boot) != SECTOR or at < 0 or at + len(BOOT_MARKER) + 4 > 446 or boot.count(BOOT_MARKER) != 1:
        raise ValueError('invalid BIOS boot sector or missing partition-offset marker')
    struct.pack_into('<I', boot, at + len(BOOT_MARKER), first)
    return boot

def write_gpt(output, partitions, sectors, boot):
    """Write both GPT copies; partition tuples are (slot, GUID, first, length, label)."""
    entries = bytearray(128 * 128)
    for slot, guid, first, length, label in partitions:
        if not 1 <= slot <= 128 or not length or first < 34 or first + length > sectors - 33:
            raise ValueError('partition exceeds the disk layout')
        entry = struct.pack('<16s16sQQQ72s', guid, uuid.uuid5(NAMESPACE, label).bytes_le,
            first, first + length - 1, 0, label.encode('utf-16le').ljust(72, b'\0'))
        entries[(slot - 1) * 128:slot * 128] = entry
    crc = zlib.crc32(entries)
    def header(own, other, array):
        result = bytearray(struct.pack('<8sIIIIQQQQ16sQIII', b'EFI PART', 0x10000, 92, 0, 0,
            own, other, 34, sectors - 34, NAMESPACE.bytes_le, array, 128, 128, crc))
        struct.pack_into('<I', result, 16, zlib.crc32(result))
        return result.ljust(SECTOR, b'\0')
    mbr = bytearray(boot)
    mbr[446:510] = bytes(64)
    mbr[446:462] = struct.pack('<B3sB3sII', 0, b'\0\2\0', 0xee, b'\xff'*3, 1, sectors - 1)
    mbr[510:512] = b'\x55\xaa'
    output.seek(0); output.write(mbr)
    output.write(header(1, sectors - 1, 2)); output.write(entries)
    output.seek((sectors - 33) * SECTOR); output.write(entries)
    output.write(header(sectors - 1, 1, sectors - 33))

def create(destination, system_payload, boot):
    size = Path(system_payload).stat().st_size
    if size % SECTOR:
        raise ValueError('system volume is not sector aligned')
    length = size // SECTOR
    sectors = (SYSTEM_FIRST + length + 33 + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT
    with Path(destination).open('wb') as output, Path(system_payload).open('rb') as source:
        output.truncate(sectors * SECTOR)
        write_gpt(output, [(1, EFI_GUID, EFI_FIRST, EFI_SECTORS, 'SystemZero boot'),
            (2, ESFS_GUID, SYSTEM_FIRST, length, 'SystemZero system')], sectors, boot_sector(boot, SYSTEM_FIRST))
        output.seek(SYSTEM_FIRST * SECTOR)
        shutil.copyfileobj(source, output)

def install_boot(image, info, boot, loader, kernel, esp):
    if info['table'] != 2 or info['readonly']:
        raise ValueError('system disk must have a writable GPT layout')
    candidates = [p for p in info['partitions'] if p['guid'] == EFI_GUID.hex()]
    if len(candidates) != 1:
        raise ValueError('system disk must contain exactly one EFI boot partition')
    efi = candidates[0]
    if Path(esp).stat().st_size != efi['sectors'] * SECTOR or efi['first'] != EFI_FIRST:
        raise ValueError('unsupported EFI partition geometry; existing image was preserved')
    first = info['first']
    code = boot_sector(boot, first)
    with Path(image).open('r+b') as output:
        # Keep GPT metadata and every other partition intact.
        output.seek(0); output.write(code[:446])
        output.seek(first * SECTOR); output.write(bytes(200 * SECTOR))
        output.seek(first * SECTOR); output.write(code)
        output.seek((first + 1) * SECTOR); output.write(loader)
        output.seek((first + 63) * SECTOR); output.write(struct.pack('<3I', 0x535A4B31, 64, len(kernel)))
        output.seek((first + 64) * SECTOR); output.write(kernel)
        output.seek(efi['first'] * SECTOR)
        with Path(esp).open('rb') as source:
            shutil.copyfileobj(source, output)
