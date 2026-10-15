#!/usr/bin/env python3
"""Verify installed layout, PATH isolation, HOME, derived values, and EFI disk identity."""
from pathlib import Path
import subprocess
import tempfile
from qemu_smoke import Guest, ROOT
from qemu_uefi import UefiGuest

def main(uefi=False):
    original = (ROOT / 'os-image.bin').read_bytes()
    with tempfile.TemporaryDirectory(prefix='systemzero-layout-qemu-') as temp:
        scratch = Path(temp); image = scratch / 'disk.bin'; image.write_bytes(original)
        writer = str(ROOT / 'ESFS_raw_write')
        for name in ('systemzero.cfg', 'sys/config/legacy.cfg', 'sys/config/environment'):
            subprocess.run([writer, '--delete', str(image), name], check=True)
        guard = scratch / 'guard.sz'
        guard.write_text('int main(){if(volume_format(0,1)!=-1 || volume_format(0,2)!=-1 || disk_edit(0,0,2)!=-1) return 1; print("boot-volumes-protected\\n"); return 0;}\n')
        subprocess.run([writer, '--replace', str(image), 'home/guard.sz', str(guard)], check=True)
        for program in (ROOT / 'external_programs').glob('*.c'):
            output = scratch / 'extracted'
            assert subprocess.run([writer, '--extract', str(image), program.stem, str(output)], capture_output=True).returncode == 1
            subprocess.run([writer, '--extract', str(image), 'bin/' + program.stem, str(output)], check=True)
            assert output.read_bytes() == program.with_suffix('.elf').read_bytes()
        launch = lambda: UefiGuest(image, scratch, 'nvme', True) if uefi else Guest(image, temp)
        guest = launch()
        try:
            guest.wait_for('DISK0p2/home>', timeout=30)
            import struct
            runtime = struct.unpack('<I', guest.memory(guest.env_address + 40, 4))[0]
            disks = struct.unpack('<20I', guest.memory(runtime, 80))
            assert sum(bool(disks[i * 5]) for i in range(4)) == 1, disks
            guest.run('pwd', '0p2:/home')
            for name, value in (('PATH', 'sys:/bin'), ('HOME', 'sys:/home'), ('INCLUDE', 'sys:/sys/include'), ('LIBRARY_PATH', 'sys:/sys/lib'), ('PWD', '0p2:/home'), ('SYSTEM_VOLUME', '0p2:')):
                guest.run('set ' + name, name + '=' + value)
            guest.run('list sys:/bin', 'settings')
            guest.run('list sys:/docs', 'manual.txt')
            guest.run('cc examples/hello.sz demo', 'Compiled')
            guest.run('demo', 'Command not found in PATH.')
            guest.run('./demo', 'compiled inside SystemZero')
            guest.run('cc guard.sz guard', 'Compiled')
            guest.run('./guard', 'boot-volumes-protected')
            guest.run('mkdir work')
            guest.run('set HOME sys:/home/work', 'Saved HOME')
            guest.run('cd')
            guest.run('set PWD', 'PWD=0p2:/home/work')
            guest.run('set HOME relative', 'Invalid environment setting')
            for name in ('PWD', 'VERSION', 'SYSTEM_VOLUME'):
                guest.run('set ' + name + ' fake', 'Invalid environment setting')
            guest.run('set NOTE saved-layout', 'Saved NOTE')
            guest.type('diskman'); guest.wait_for('Disk Partitions and Formatting')
            guest.wait_for('FAT32 (EFI boot)')
            guest.wait_for('ESFS')
            guest.key('up'); guest.key('f')
            guest.wait_for('The EFI boot partition cannot be formatted.')
            guest.key('esc'); guest.wait_for('DISK0p2/home/work>')
            guest.run('set PATH sys:/home/work', 'Saved PATH')
            guest.run('list', 'Command not found in PATH.')
            guest.run('sys:/bin/list sys:/bin', 'settings')
            guest.run('sys:/bin/set PATH sys:/bin', 'Saved PATH')
        finally:
            guest.close()
        guest = launch()
        try:
            guest.wait_for('DISK0p2/home/work>', timeout=30)
            guest.run('set NOTE', 'NOTE=saved-layout')
            guest.run('set PATH', 'PATH=sys:/bin')
            guest.run('cd /'); guest.run('cd')
            guest.run('pwd', '0p2:/home/work')
            guest.run('sys:/home/demo', 'compiled inside SystemZero')
        finally:
            guest.close()
        subprocess.run([writer, '--check', str(image)], check=True)
    assert (ROOT / 'os-image.bin').read_bytes() == original
    print('PASS layout: /bin only, PATH, HOME, read-only derived values, persistence, EFI identity', flush=True)

if __name__ == '__main__':
    import sys
    main('--uefi' in sys.argv)
