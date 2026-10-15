#!/usr/bin/env python3
"""Exercise partitions, directories, environment/PATH, SDK headers and linking."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from qemu_smoke import Guest,ROOT
from qemu_uefi import UefiGuest
from qemu_threads import wait

def utility(guest,key,expected='Changes applied.'):
    guest.key(key); guest.key('ret'); guest.wait_for(expected)

def main(uefi=False,source_image=ROOT/'os-image.bin'):
    original=source_image.read_bytes()
    with tempfile.TemporaryDirectory(prefix='systemzero-volumes-qemu-') as temp:
        scratch=Path(temp); image=scratch/'system.bin'; image.write_bytes(original)
        extra=scratch/'data.bin'
        with extra.open('wb') as file: file.truncate(32*1024*1024)
        writer=str(ROOT/'ESFS_raw_write')
        for name in ('systemzero.cfg','sys/config/environment'):
            subprocess.run([writer,'--delete',str(image),name],check=True)
        payload=scratch/'payload'; payload.write_bytes(bytes(range(256))*400+b'boundary')
        subprocess.run([writer,'--replace',str(image),'payload',str(payload)],check=True)
        source=scratch/'sdk.sz'; source.write_text('#include <systemzero/io.h>\nint twice(int);\nint main(){sz_print("sdk-link-ok\\n"); print_dec(twice(21)); newline(); return 0;}\n')
        library=scratch/'math.sz'; library.write_text('int twice(int n){return n*2;}\n')
        for name,file in (('sdk.sz',source),('math.sz',library)):
            subprocess.run([writer,'--replace',str(image),name,str(file)],check=True)
        guest=UefiGuest(image,scratch,'nvme',True,'std',extra) if uefi else Guest(image,temp,extra)
        try:
            guest.boot_root(); initial=guest.heap_pointer()
            guest.run('cc -c math.sz math.o','Compiled object')
            guest.run('cc sdk.sz sdk math.o io.o','Compiled')
            guest.run('./sdk','sdk-link-ok'); assert '42' in guest.screen()
            guest.run('cc -c sdk.sz sdk.o','Compiled object')
            guest.run('ld sdk2 sdk.o math.o system.a','Compiled'); guest.run('./sdk2','sdk-link-ok')
            assert 'kernel/' not in guest.run('list sys:/sys')
            guest.run('cc sdk.sz missing io.o','Unresolved symbol: twice')
            guest.type('diskman'); guest.wait_for('Disk Partitions and Formatting'); guest.key('right')
            utility(guest,'g'); utility(guest,'c'); utility(guest,'f')
            utility(guest,'c'); guest.key('down'); utility(guest,'f')
            guest.screenshot(ROOT/'build/diskman-preview.png'); guest.key('esc'); guest.wait_for('DISK0p2>')
            guest.run('mkdir 1p1:/home'); guest.run('mkdir 1p1:/home/source')
            guest.run('copy sys:payload 1p1:/home/source/data')
            guest.run('copy sys:sdk 1p1:/home/app')
            guest.run('copy 1p1:/home/source/data 1p2:/copied')
            guest.run('cd 1p1:/home/source'); guest.run('pwd','1p1:/home/source')
            guest.run('list','data'); guest.run('cd ..'); guest.run('list','source/')
            guest.run('del source','Cannot remove path'); guest.run('cd /')
            guest.run('set PATH 1p1:/home;sys:/bin','Saved PATH')
            guest.run('app','sdk-link-ok')
            guest.run('sdk','Command not found in PATH.')
            guest.run('sys:/sdk','sdk-link-ok')
            guest.run('set COLOR_SECONDARY 10','Saved COLOR_SECONDARY')
            assert guest.desktop_state()['secondary_color']==10
            guest.run('set COLOR_BACKGROUND 15','Invalid environment setting')
            assert guest.desktop_state()['color']==31
            guest.type('env'); guest.wait_for('System Environment Variables')
            guest.key('ctrl','n'); guest.type('NOTE',submit=False); guest.key('tab')
            guest.type('saved through GUI',submit=False); guest.key('ctrl','s'); guest.wait_for('Saved.')
            guest.screenshot(ROOT/'build/environment-preview.png')
            guest.key('esc'); guest.wait_for('DISK1p1>')
            guest.run('set NOTE','NOTE=saved through GUI')
            guest.run('drive sys'); wait(lambda: guest.heap_pointer()==initial,'Command heap leaked')
        except BaseException:
            print(guest.screen()); guest.screenshot(ROOT/'build/volumes-failure.png'); raise
        finally: guest.close()
        output=scratch/'extracted'
        for partition,path in ((1,'home/source/data'),(2,'copied')):
            subprocess.run([writer,'--volume',str(partition),'--extract',str(extra),path,str(output)],check=True)
            assert output.read_bytes()==payload.read_bytes()
            subprocess.run([writer,'--volume',str(partition),'--check',str(extra)],check=True)
        guest=UefiGuest(image,scratch,'nvme',True,'std',extra) if uefi else Guest(image,temp,extra)
        try:
            guest.boot_root(); assert guest.desktop_state()['secondary_color']==10
            guest.run('set NOTE','NOTE=saved through GUI')
            guest.run('app','sdk-link-ok'); guest.run('sdk','Command not found in PATH.')
            guest.run('drive 1p2'); guest.run('list','copied')
            guest.type('diskman'); guest.wait_for('Disk Partitions and Formatting')
            utility(guest,'d'); guest.key('esc'); guest.wait_for('DISK1p1>')
            guest.run('list','home/')
        finally: guest.close()
        subprocess.run([writer,'--volume','1','--extract',str(extra),'home/source/data',str(output)],check=True)
        assert output.read_bytes()==payload.read_bytes()
        assert subprocess.run([writer,'--volume','2','--check',str(extra)],capture_output=True).returncode==1
        subprocess.run([writer,'--check',str(image)],check=True)
    assert source_image.read_bytes()==original
    print('PASS volumes: partitioning, formatting, directories, PATH, environment GUI, SDK linking, restart durability',flush=True)

if __name__=='__main__':
    import sys
    main('--uefi' in sys.argv)
