#!/usr/bin/env python3
"""Check blank input, configurable completion and zedit's logical-line gutter."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from qemu_smoke import Guest, ROOT, COLS
from qemu_uefi import UefiGuest
from qemu_zedit import TextGuest, settled
from qemu_shell import expect_input, submit
from qemu_mouse import Mouse

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--uefi',action='store_true'); parser.add_argument('--text',action='store_true')
    parser.add_argument('--image',type=Path,default=ROOT/'os-image.bin'); opt=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='systemzero-completion-') as temp:
        scratch=Path(temp); image=scratch/'disk.bin'; shutil.copyfile(opt.image,image)
        writer=str(ROOT/'ESFS_raw_write'); data=scratch/'data'
        def tool(*args): subprocess.run([writer,*map(str,args)],check=True,stdout=subprocess.DEVNULL)
        for name in ('home/complete','home/complete/alpine','home/complete/sub dir'): tool('--mkdir',image,name)
        data.write_text('int main(){print("space-run-ok");return 0;}\n')
        for name in ('alpha.sz','alps.sz','two words.sz','sub dir/file name.sz'): tool('--replace',image,'home/complete/'+name,data)
        code=b'//'+b'x'*110+b'\nint value = 1;\n'+b''.join(f'// line {i}\n'.encode() for i in range(3,115))
        data.write_bytes(code); tool('--replace',image,'home/numbered.sz',data)
        guest=UefiGuest(image,scratch,'nvme',True) if opt.uefi else TextGuest(image,temp,vga='cirrus') if opt.text else Guest(image,temp)
        try:
            guest.wait_for('DISK0p2/home>'); baseline=guest.heap_pointer()
            guest.run('cls')
            for blank in ('','   ',''):
                guest.type(blank); time.sleep(.1)
                lines=[line.strip() for line in guest.screen().splitlines() if line.strip()]
                assert all(line=='DISK0p2/home>' for line in lines),lines
            guest.key('up'); expect_input(guest,'cls'); guest.key('esc')
            # Command names come from PATH, rather than all files in a directory.
            guest.type('hexed',submit=False); guest.key('tab'); expect_input(guest,'hexedit'); guest.key('esc')
            guest.type('list complete/al',submit=False); guest.key('tab'); expect_input(guest,'list complete/alp')
            guest.key('tab'); expect_input(guest,'list complete/alpha.sz')
            guest.key('tab'); expect_input(guest,'list complete/alpine/')
            guest.key('shift','tab'); expect_input(guest,'list complete/alpha.sz'); guest.key('esc')
            guest.type('cd complete/al',submit=False); guest.key('tab'); expect_input(guest,'cd complete/alpine/')
            submit(guest,'cd complete/alpine/'); guest.run('cd sys:/home')
            guest.type('list sys:/ho',submit=False); guest.key('tab'); expect_input(guest,'list sys:/home/')
            submit(guest,'list sys:/home/','complete/')
            # Unique names containing spaces are quoted and accepted by every editor.
            guest.type('edit complete/two',submit=False); guest.key('tab'); expect_input(guest,'edit "complete/two words.sz"')
            guest.key('ret'); guest.wait_for('Ctrl+S: save'); assert guest.screen().splitlines()[0].startswith('int main()')
            guest.key('esc'); settled(guest,baseline)
            guest.type('view complete/sub',submit=False); guest.key('tab'); expect_input(guest,'view "complete/sub dir/"')
            guest.type('fi',submit=False); guest.key('tab'); expect_input(guest,'view "complete/sub dir/file name.sz"')
            guest.key('ret'); guest.wait_for('PgUp/PgDn: scroll'); assert guest.screen().splitlines()[0].startswith('int main()')
            guest.key('esc'); settled(guest,baseline)
            # Complete an argument at the caret, keeping later arguments.
            guest.type('copy complete/alpha.sz copy.sz',submit=False); guest.key('home')
            for _ in range(len('copy complete/al')): guest.key('right')
            guest.key('tab'); expect_input(guest,'copy complete/alp copy.sz')
            guest.key('tab'); expect_input(guest,'copy complete/alpha.sz copy.sz')
            submit(guest,'copy complete/alpha.sz copy.sz','Copied')
            # The configuration can be changed in the guest and takes effect immediately.
            guest.run('copy sys:/sys/config/completion saved-completion','Copied')
            guest.type('zedit sys:/sys/config/completion')
            guest.wait_for('F5 Run'); guest.key('ctrl','end'); guest.type('\npwd=paths\nlist=none\n',submit=False)
            guest.key('ctrl','s'); guest.key('esc'); settled(guest,baseline)
            guest.type('list complete/two',submit=False); guest.key('tab'); expect_input(guest,'list complete/two'); guest.key('esc')
            guest.type('pwd complete/two',submit=False); guest.key('tab'); expect_input(guest,'pwd "complete/two words.sz"'); guest.key('esc')
            guest.run('del sys:/sys/config/completion')
            guest.run('copy saved-completion sys:/sys/config/completion','Copied')
            # Logical line numbers do not increment for visually wrapped source lines.
            guest.type('zedit numbered.sz'); guest.wait_for('F5 Run'); lines=guest.screen().splitlines()
            assert lines[0].startswith('  1 | //') and lines[1].startswith('    | '),lines[:3]
            assert lines[2].startswith('  2 | int value'),lines[:3]
            mouse=Mouse(guest); mouse.click(2,6+len('int ')); guest.type('X',submit=False)
            assert guest.screen().splitlines()[2].startswith('  2 | int Xvalue'),guest.screen()
            mouse.wheel(False); assert guest.screen().splitlines()[0].startswith('  3 | '),guest.screen()
            guest.key('ctrl','end'); assert any('114 | ' in line for line in guest.screen().splitlines()),guest.screen()
            guest.key('ctrl','home'); guest.key('down'); guest.key('home'); guest.key('delete')
            assert guest.screen().splitlines()[1].startswith('    | '),guest.screen()
            guest.key('ctrl','s'); guest.key('esc'); settled(guest,baseline)
            guest.type('zedit complete/two',submit=False); guest.key('tab'); expect_input(guest,'zedit "complete/two words.sz"')
            guest.key('ret'); guest.wait_for('F5 Run'); guest.key('f5'); guest.wait_for('space-run-ok'); guest.wait_for('Press any key')
            guest.key('spc'); guest.wait_for('F5 Run'); guest.key('esc'); settled(guest,baseline)
            if not opt.text:
                guest.type('zedit numbered.sz'); guest.wait_for('F5 Run'); guest.screenshot(ROOT/'build/zedit-line-numbers.png'); guest.key('esc'); settled(guest,baseline)
        except BaseException:
            print(guest.screen()); raise
        finally: guest.close()
        tool('--check',image)
    print('Completion, blank input and line numbers passed ('+('UEFI USB' if opt.uefi else 'VGA text' if opt.text else 'BIOS')+').',flush=True)
if __name__=='__main__': main()
