"""Validate the portable GPT/FAT32 boot image independently of firmware."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import uuid
import zlib

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('build_uefi',ROOT/'scripts/build_uefi.py')
builder=importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
spec=importlib.util.spec_from_file_location('disk_image',ROOT/'scripts/disk_image.py')
disk_builder=importlib.util.module_from_spec(spec); spec.loader.exec_module(disk_builder)

class UefiImageChecks(unittest.TestCase):
    def test_gpt_fat32_and_multicluster_loader(self):
        payload=b'MZ'+bytes(range(256))*513+b'last-byte'
        with tempfile.TemporaryDirectory(prefix='sz-esp-') as scratch:
            scratch=Path(scratch); raw=scratch/'system.bin'; esp=scratch/'esp.bin'; path=scratch/'disk.bin'
            with raw.open('wb') as output: output.truncate(100*1024*1024)
            builder.build(payload,esp)
            boot=disk_builder.BOOT_MARKER + bytes(510-len(disk_builder.BOOT_MARKER)) + b'\x55\xaa'
            disk_builder.create(path,raw,boot)
            info={'table':2,'readonly':0,'first':disk_builder.SYSTEM_FIRST,
                'partitions':[{'first':2048,'sectors':131072,'guid':disk_builder.EFI_GUID.hex()}]}
            disk_builder.install_boot(path,info,boot,b'BIOS loader',b'kernel ELF',esp)
            data=path.read_bytes()
        sectors=len(data)//512
        self.assertEqual(len(data),166*1024*1024)
        system=struct.unpack_from('<16s16sQQQ72s',data,1024+128)
        self.assertEqual(system[0],disk_builder.ESFS_GUID)
        self.assertEqual(system[2],disk_builder.SYSTEM_FIRST)
        self.assertEqual(system[3]-system[2]+1,204800)
        marker=data.index(disk_builder.BOOT_MARKER)
        self.assertEqual(struct.unpack_from('<I',data,marker+8)[0],system[2])
        self.assertEqual(struct.unpack_from('<3I',data,(system[2]+63)*512),(0x535a4b31,64,10))
        self.assertEqual(data[(system[2]+64)*512:(system[2]+64)*512+10],b'kernel ELF')
        self.assertEqual(data[510:512],b'\x55\xaa')
        for sector,backup in ((1,sectors-1),(sectors-1,1)):
            header=bytearray(data[sector*512:sector*512+92])
            values=struct.unpack('<8sIIIIQQQQ16sQIII',header)
            self.assertEqual(values[:3],(b'EFI PART',0x10000,92))
            self.assertEqual(values[5:7],(sector,backup))
            crc=values[3]; struct.pack_into('<I',header,16,0)
            self.assertEqual(crc,zlib.crc32(header))
            array=data[values[10]*512:values[10]*512+values[11]*values[12]]
            self.assertEqual(values[13],zlib.crc32(array))
        partition=struct.unpack_from('<16s16sQQQ72s',data,1024)
        self.assertEqual(uuid.UUID(bytes_le=partition[0]),uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b'))
        base=partition[2]*512
        self.assertEqual(struct.unpack_from('<H',data,base+11)[0],512)
        reserved=struct.unpack_from('<H',data,base+14)[0]
        copies=data[base+16]; fat_sectors=struct.unpack_from('<I',data,base+36)[0]
        fat_start=base+reserved*512; fat=data[fat_start:fat_start+fat_sectors*512]
        self.assertEqual(fat,data[fat_start+len(fat):fat_start+len(fat)*2])
        first=base+(reserved+copies*fat_sectors)*512
        def directory(cluster,name):
            start=first+(cluster-2)*512
            for at in range(start,start+512,32):
                if data[at:at+11]==name:
                    low=struct.unpack_from('<H',data,at+26)[0]
                    high=struct.unpack_from('<H',data,at+20)[0]
                    return low|(high<<16),struct.unpack_from('<I',data,at+28)[0]
            self.fail(f'Missing FAT directory entry {name!r}')
        efi,_=directory(2,b'EFI        '); boot,_=directory(efi,b'BOOT       ')
        cluster,size=directory(boot,b'BOOTX64 EFI'); content=bytearray(); seen=set()
        while cluster<0xffffff8:
            self.assertNotIn(cluster,seen); seen.add(cluster)
            start=first+(cluster-2)*512; content.extend(data[start:start+512])
            cluster=struct.unpack_from('<I',fat,cluster*4)[0]&0xfffffff
        self.assertEqual(bytes(content[:size]),payload)
        self.assertGreater(len(seen),255)

if __name__=='__main__': unittest.main()
