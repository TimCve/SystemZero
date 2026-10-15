"""Check that DVD and USB metadata point to the same complete FAT payload."""
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
import uuid
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import build_installer

class InstallerImageChecks(unittest.TestCase):
    @unittest.skipUnless(shutil.which('genisoimage') or shutil.which('mkisofs'),'requires genisoimage')
    def test_optical_catalog_usb_gpt_and_complete_payload(self):
        with tempfile.TemporaryDirectory(prefix='sz-install-iso-') as temp:
            temp=Path(temp); loader=temp/'loader'; payload=temp/'payload'; iso=temp/'install.iso'
            efi=b'MZ'+bytes(range(256))*19; system=bytes(range(251))*9001+b'installer-tail'
            loader.write_bytes(efi); payload.write_bytes(system)
            build_installer.build(loader,payload,iso)
            data=iso.read_bytes(); sectors=len(data)//512
            self.assertEqual(data[16*2048+1:16*2048+6],b'CD001')
            for lba,other in ((1,sectors-1),(sectors-1,1)):
                h=bytearray(data[lba*512:lba*512+92]); expected=struct.unpack_from('<I',h,16)[0]
                struct.pack_into('<I',h,16,0)
                self.assertEqual(zlib.crc32(h),expected)
                self.assertEqual(struct.unpack_from('<QQ',h,24),(lba,other))
                array,count,size,crc=struct.unpack_from('<QIII',h,72)
                self.assertEqual(zlib.crc32(data[array*512:array*512+count*size]),crc)
            self.assertEqual(uuid.UUID(bytes_le=data[1024:1040]),uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b'))
            first,last=struct.unpack_from('<QQ',data,1024+32)
            catalog=struct.unpack_from('<I',data,17*2048+71)[0]*2048
            self.assertEqual(data[catalog:catalog+2],b'\x01\xef')
            self.assertEqual(sum(struct.unpack_from('<16H',data,catalog))&65535,0)
            self.assertEqual(struct.unpack_from('<I',data,catalog+40)[0]*4,first)
            fat=data[first*512:(last+1)*512]
            self.assertEqual(struct.unpack_from('<I',fat,32)[0]*512,len(fat))
            reserved=struct.unpack_from('<H',fat,14)[0]; blocks=struct.unpack_from('<I',fat,36)[0]
            table=fat[reserved*512:(reserved+blocks)*512]
            self.assertEqual(table,fat[(reserved+blocks)*512:(reserved+2*blocks)*512])
            base=(reserved+2*blocks)*512
            def content(cluster,size):
                result=bytearray(); seen=set()
                while cluster<0xffffff8:
                    self.assertNotIn(cluster,seen); seen.add(cluster)
                    start=base+(cluster-2)*512; result.extend(fat[start:start+512])
                    cluster=struct.unpack_from('<I',table,cluster*4)[0]&0xfffffff
                return bytes(result[:size])
            def entry(directory,name):
                for at in range(0,len(directory),32):
                    if directory[at:at+11]==name:
                        cluster=struct.unpack_from('<H',directory,at+26)[0]|struct.unpack_from('<H',directory,at+20)[0]<<16
                        return cluster,struct.unpack_from('<I',directory,at+28)[0]
                self.fail(f'Missing {name!r}')
            root=content(2,512)
            self.assertEqual(content(*entry(root,b'SYSTEM  IMG')),system)
            efi_dir=content(entry(root,b'EFI        ')[0],512)
            boot_dir=content(entry(efi_dir,b'BOOT       ')[0],512)
            self.assertEqual(content(*entry(boot_dir,b'BOOTX64 EFI')),efi)

if __name__=='__main__': unittest.main()
