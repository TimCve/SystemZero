#!/usr/bin/env python3
"""Exercise the external shell and zedit against disposable boot disks."""
import argparse
import re
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
from qemu_smoke import Guest, ROOT, COLS, ROWS
from qemu_uefi import UefiGuest

SOURCE = ('#include <systemzero/io.h>\n'
          'int add(int left, int right) { return left + right; }\n'
          'int main() {\n'
          '  int local_value = 4;\n'
          '  sz_print("zedit-run-ok\\n");\n'
          '  print_dec(add(local_value, 3));\n'
          '  return 0;\n'
          '}\n')

class TextGuest(Guest):
    def run(self,text,expected=None):
        self.type(text)
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            screen=self.screen(); start=screen.rfind('> '+text)
            tail=screen[start+len(text)+2:] if start>=0 else screen
            if re.search(r'DISK[0-3](?:p\d+)?(?:/[^\n]*)?>\s*$',tail) and self.desktop_state()['command']=='shell':
                if expected is not None: assert expected in tail,tail
                return tail
            time.sleep(.05)
        raise AssertionError(f'Command {text!r} did not finish:\n{screen}')

def cells(guest):
    address=guest.terminal_address()
    return guest.memory(address, COLS*ROWS*2) if address else guest.memory(0xb8000+160,80*24*2)

def caret(guest):
    return guest.terminal_state()['cursor'] if guest.terminal_address() else None

def settled(guest, heap, command='shell'):
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        if guest.heap_pointer()==heap and guest.desktop_state()['command']==command: return
        time.sleep(.02)
    raise AssertionError(f'Heap was not reclaimed: {guest.heap_pointer():x}, expected {heap:x}')

def position(guest, source, offset):
    guest.key('ctrl','home')
    for _ in range(offset): guest.key('right')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--uefi',action='store_true')
    parser.add_argument('--text',action='store_true')
    parser.add_argument('--image',type=Path,default=ROOT/'os-image.bin')
    options=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='systemzero-zedit-qemu-') as scratch:
        scratch=Path(scratch); image=scratch/'disk.bin'; shutil.copyfile(options.image,image)
        source=scratch/'ide.sz'; source.write_text(SOURCE)
        bad=scratch/'bad.sz'; bad.write_text('int main() { int x = ; }\n')
        marker=scratch/'marker.sz'; marker.write_text('int main(){file_write("should-not-run", "bad", 3);return 0;}\n')
        interactive=scratch/'interactive.sz'
        interactive.write_text('#include <systemzero/io.h>\n'
            'int worker(int value){thread_sleep(10);print("worker-run-ok\\n");return value;}\n'
            'int main(){thread_start(worker,0);sz_print("Key: ");int key=getchar();'
            'print_dec(key);draw_line(20,80,40,80,14);chdir("sys:/");return 0;}\n')
        writer=str(ROOT/'ESFS_raw_write')
        for name,payload in (('home/ide.sz',source),('home/bad.sz',bad),('home/marker.sz',marker),('home/interactive.sz',interactive)):
            subprocess.run([writer,'--replace',str(image),name,str(payload)],check=True,stdout=subprocess.DEVNULL)
        guest=UefiGuest(image,scratch,'ahci',True) if options.uefi else TextGuest(image,str(scratch),vga='cirrus') if options.text else Guest(image,str(scratch))
        try:
            guest.wait_for('DISK0p2/home>')
            # A live executable load means the foreground heap contains shell's
            # ELF image; the kernel has no built-in REPL fallback.
            baseline=guest.heap_pointer()
            guest.run('list sys:/bin','shell')
            guest.run('list sys:/bin','zedit')
            assert guest.heap_pointer()==baseline
            # A nested interactive shell returns to the caller on exit.
            guest.type('shell'); guest.wait_for('Sources: sys:/home/examples.')
            time.sleep(.3); guest.run('pwd','0p2:/home')
            guest.run('exit'); settled(guest,baseline)
            guest.type('zedit ide.sz'); guest.wait_for('F5 Run')
            editor_heap=guest.heap_pointer()
            editor_command=guest.desktop_state()['command']
            if not options.text:
                data=cells(guest); attrs=data[1::2]
                assert len(set(attrs[:len(SOURCE)]))>=4, 'Source is not syntax highlighted'
            # Built-in completion displays arguments and provenance.
            guest.key('ctrl','end'); guest.type('pri',submit=False)
            screen=guest.wait_for('built-in kernel API')
            assert 'int print(char*)' in screen,screen
            guest.key('tab')
            assert guest.screen().splitlines()[8][6:].startswith('print')
            for _ in range(5): guest.key('backspace')
            guest.key('esc') if 'Completion:' in guest.screen() else None
            # The source body offers visible variables and local signatures.
            at=SOURCE.index('  return 0;')
            position(guest,SOURCE,at)
            guest.type('local_v',submit=False); screen=guest.wait_for('local variable')
            assert 'int local_value' in screen
            guest.key('tab')
            for _ in range(len('local_value')): guest.key('backspace')
            guest.type('ad',submit=False); screen=guest.wait_for('local function')
            assert 'int add(int left, int right)' in screen
            guest.key('tab')
            for _ in range(3): guest.key('backspace')
            guest.type('sz_pr',submit=False); screen=guest.wait_for('systemzero/io.h')
            assert 'int sz_print(char*)' in screen
            guest.screenshot(ROOT/'build/zedit-completion.png')
            guest.key('tab')
            for _ in range(len('sz_print')): guest.key('backspace')
            # Running preserves the complete editor view and caret. Temporary
            # ELF/compiler/snapshot memory is released on every return.
            guest.key('esc') if 'Completion:' in guest.screen() else None
            before=cells(guest); cursor=caret(guest)
            for _ in range(3):
                guest.key('f5'); screen=guest.wait_for('Press any key to return to zedit.',30)
                assert 'zedit-run-ok' in screen and '7' in screen,screen
                assert '-> exit status' not in screen
                guest.key('spc'); guest.wait_for('Returned from run')
                assert cells(guest)[:-(COLS if not options.text else 80)*2]==before[:-(COLS if not options.text else 80)*2]
                assert caret(guest)==cursor
                settled(guest,editor_heap,editor_command)
            guest.screenshot(ROOT/'build/zedit.png')
            guest.key('esc'); guest.wait_for('DISK0p2/home>')
            settled(guest,baseline)
            assert 'int main()' not in guest.screen(), 'zedit did not restore the console'
            # Build an existing output, then make compilation fail. It must not
            # execute the previous program or leave the editor/run UI stuck.
            guest.run('cc marker.sz bad')
            guest.type('zedit bad.sz'); guest.wait_for('F5 Run'); guest.key('f5')
            screen=guest.wait_for('Press any key to return to zedit.')
            assert 'bad.sz:1:' in screen and 'Run:' not in screen,screen
            guest.key('ret'); guest.wait_for('Returned from run'); guest.key('esc'); guest.wait_for('DISK0p2/home>')
            assert 'should-not-run' not in guest.run('list'), 'A failed build ran a stale executable'
            settled(guest,baseline)
            # Explicit group linking, keyboard input, joined workers, graphics,
            # and cwd changes work through the modal shell as normal APIs do.
            guest.type('zedit interactive.sz io.o'); guest.wait_for('F5 Run')
            pixel=None
            if not options.text:
                state=guest.terminal_state(); pixel_at=state['pixels']+(80*state['pitch']+20)*4
                pixel=guest.memory(pixel_at,4)
            guest.key('f5'); guest.wait_for('Key:'); guest.key('x')
            screen=guest.wait_for('Press any key to return to zedit.')
            assert 'worker-run-ok' in screen and '120' in screen,screen
            if pixel is not None: assert struct.unpack('<I',guest.memory(pixel_at,4))[0]==0xffff55
            guest.key('spc'); guest.wait_for('Returned from run')
            if pixel is not None: assert guest.memory(pixel_at,4)==pixel, 'Run graphics were not restored'
            guest.key('esc'); guest.wait_for('DISK0p2/home>'); settled(guest,baseline)
            # New source needs no touch command; built-ins compile without
            # explicit imports, while the default SDK library links sz_* calls.
            guest.type('zedit new.sz'); guest.wait_for('F5 Run')
            guest.type('int main(){print("new-source-ok");return 0;}\n',submit=False)
            guest.key('f5'); guest.wait_for('new-source-ok'); guest.wait_for('Press any key to return to zedit.')
            guest.key('spc'); guest.wait_for('Returned from run'); guest.key('esc'); guest.wait_for('DISK0p2/home>')
            settled(guest,baseline)
        except BaseException:
            print(guest.screen())
            print(guest.command('human-monitor-command',{'command-line':'info registers'}))
            raise
        finally: guest.close()
        restored=scratch/'saved'; subprocess.run([writer,'--extract',str(image),'home/ide.sz',str(restored)],check=True)
        assert restored.read_text()==SOURCE, 'Completion or run modified unrelated source'
        subprocess.run([writer,'--check',str(image)],check=True)
        # Missing startup shell is a clear boot error rather than a hidden REPL.
        if not options.uefi and not options.text:
            subprocess.run([writer,'--delete',str(image),'bin/shell'],check=True)
            guest=Guest(image,str(scratch))
            try:
                guest.wait_for('Cannot start sys:/bin/shell.')
                assert 'DISK0p2/home>' not in guest.screen()
            finally: guest.close()
    print('External shell and zedit checks passed ('+('UEFI/AHCI/USB' if options.uefi else 'VGA text' if options.text else 'BIOS graphics')+').')

if __name__=='__main__': main()
