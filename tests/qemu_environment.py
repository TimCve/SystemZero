#!/usr/bin/env python3
"""Check environment GUI saves, console restoration, and live theme application."""
from pathlib import Path
import subprocess
import tempfile
from qemu_smoke import Guest,ROOT
from qemu_uefi import UefiGuest
from qemu_threads import wait,secondary_cells

def main(uefi=False):
    original=(ROOT/'os-image.bin').read_bytes()
    with tempfile.TemporaryDirectory(prefix='systemzero-environment-qemu-') as temp:
        scratch=Path(temp); image=scratch/'disk.bin'; image.write_bytes(original)
        for name in ('systemzero.cfg','sys/config/environment'):
            subprocess.run([str(ROOT/'ESFS_raw_write'),'--delete',str(image),name],check=True)
        guest=UefiGuest(image,scratch,'nvme',True) if uefi else Guest(image,temp)
        try:
            guest.boot_root(); initial=guest.heap_pointer()
            guest.run('drawline 10 30 10 30 14')
            terminal=guest.terminal_state(); address=terminal['pixels']+(30*terminal['pitch']+10)*4
            pixel=guest.memory(address,4)
            guest.type('env'); guest.wait_for('System Environment Variables')
            guest.key('ctrl','n'); guest.type('NOTE',submit=False); guest.key('tab')
            guest.type('graphics preserved',submit=False); guest.key('ctrl','s'); guest.wait_for('Saved.')
            guest.key('esc'); guest.wait_for('DISK0p2>')
            assert guest.memory(address,4)==pixel,'Saving a custom variable erased console graphics'
            guest.run('set NOTE','NOTE=graphics preserved')
            guest.type('env'); guest.wait_for('System Environment Variables')
            for _ in range(5): guest.key('down')
            guest.key('backspace'); guest.key('backspace'); guest.type('10',submit=False)
            guest.key('ctrl','s'); guest.wait_for('Saved.'); guest.key('esc'); guest.wait_for('DISK0p2>')
            assert guest.desktop_state()['secondary_color']==10
            secondary_cells(guest,31,10)
            wait(lambda: guest.heap_pointer()==initial,'Environment GUI leaked temporary memory')
        finally: guest.close()
    assert (ROOT/'os-image.bin').read_bytes()==original
    print('PASS environment GUI: saved values, preserved graphics, updated prompt/logo colors, reclaimed memory',flush=True)

if __name__=='__main__':
    import sys
    main('--uefi' in sys.argv)
