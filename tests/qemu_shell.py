#!/usr/bin/env python3
"""Check command recall/editing and file trees on disposable boot disks."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
from qemu_smoke import Guest, ROOT
from qemu_uefi import UefiGuest
from qemu_zedit import TextGuest, settled

def current_input(guest):
    lines=guest.screen().splitlines()
    prompt=re.compile(r'DISK[0-3](?:p\d+)?(?:/[^>]*)?> ')
    for row in range(len(lines)-1,-1,-1):
        match=prompt.match(lines[row])
        if match: return ''.join([lines[row][match.end():],*lines[row+1:]]).rstrip(' ')
    return None

def expect_input(guest,text):
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        if current_input(guest)==text: return
        time.sleep(.02)
    raise AssertionError(f'Expected editable input {text!r}, got {current_input(guest)!r}:\n{guest.screen()}')

def submit(guest,command,expected=None):
    expect_input(guest,command); guest.key('ret')
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        if current_input(guest)=='' and guest.desktop_state()['command']=='shell':
            screen=guest.screen(); at=screen.rfind('> '+command)
            output=screen[at+len(command)+2:] if at>=0 else screen
            if expected is not None: assert expected in output,output
            return output
        time.sleep(.02)
    raise AssertionError(f'Recalled command did not finish:\n{guest.screen()}')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--uefi',action='store_true')
    parser.add_argument('--text',action='store_true')
    parser.add_argument('--image',type=Path,default=ROOT/'os-image.bin')
    options=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='systemzero-shell-qemu-') as temp:
        scratch=Path(temp); image=scratch/'disk.bin'; shutil.copyfile(options.image,image)
        writer=str(ROOT/'ESFS_raw_write'); payload=scratch/'payload'; payload.write_bytes(b'contents')
        extra=scratch/'extra.bin'
        with extra.open('wb') as stream: stream.truncate(8*1024*1024)
        def tool(*args): subprocess.run([writer,*map(str,args)],check=True,stdout=subprocess.DEVNULL)
        tool('--format',extra)
        for directory in ('home/tree-check','home/tree-check/alpha','home/tree-check/alpha/empty',
                          'home/tree-check/zeta','home/tree-check/zeta/sub','home/deep'):
            tool('--mkdir',image,directory)
        for name in ('alpha/a.txt','root.txt','two words.txt','zeta/sub/z.txt'):
            tool('--replace',image,'home/tree-check/'+name,payload)
        deep='home/deep'
        for _ in range(60): deep+='/a'; tool('--mkdir',image,deep)
        tool('--replace',image,deep+'/leaf',payload)
        tool('--mkdir',extra,'data'); tool('--mkdir',extra,'data/child')
        tool('--replace',extra,'data/child/extra.txt',payload)
        guest=UefiGuest(image,scratch,'nvme',True,'std',extra) if options.uefi else TextGuest(image,temp,extra,vga='cirrus') if options.text else Guest(image,temp,extra)
        try:
            guest.wait_for('DISK0p2/home>'); expect_input(guest,''); baseline=guest.heap_pointer()
            # Empty history and ordinary API readline do not invent commands.
            guest.key('up'); expect_input(guest,''); guest.key('down'); expect_input(guest,'')
            guest.run('set NOTE first','Saved NOTE'); guest.run('set NOTE second','Saved NOTE')
            guest.type('unfinished input',submit=False); guest.key('up'); expect_input(guest,'set NOTE second')
            guest.key('up'); expect_input(guest,'set NOTE first'); guest.key('up'); expect_input(guest,'set NOTE first')
            guest.key('down'); expect_input(guest,'set NOTE second'); guest.key('down'); expect_input(guest,'unfinished input')
            guest.key('down'); expect_input(guest,'unfinished input'); guest.key('esc'); expect_input(guest,'')
            # Recall with arguments; edit the value without mutating the stored
            # entry. Navigation, Delete and Backspace operate on the recalled line.
            guest.key('up'); expect_input(guest,'set NOTE second')
            for _ in range(6): guest.key('backspace')
            guest.type('third',submit=False); submit(guest,'set NOTE third','Saved NOTE')
            guest.key('up'); expect_input(guest,'set NOTE third'); guest.key('up'); expect_input(guest,'set NOTE second')
            guest.key('esc'); expect_input(guest,''); guest.run('set NOTE','NOTE=third')
            # A long command wraps and recalls at the bottom of the terminal.
            long='set NOTE '+''.join(str(i%10) for i in range(250))
            guest.run('cls')
            for _ in range(50): guest.key('ret')
            guest.run(long,'Saved NOTE')
            guest.key('up'); expect_input(guest,long)
            guest.key('home')
            for _ in range(9): guest.key('right')
            guest.key('delete'); guest.key('end'); guest.key('backspace')
            edited=long[:9]+long[10:-1]; submit(guest,edited,'Saved NOTE')
            output=guest.run('set NOTE')
            assert 'NOTE='+edited[9:] in output.replace('\n',''),output
            guest.key('up'); guest.key('up'); expect_input(guest,edited)
            guest.key('home'); guest.key('esc'); expect_input(guest,'')
            guest.key('up'); guest.key('down'); expect_input(guest,'')
            # Nested shells have independent histories; the outer history
            # survives loading and unloading commands and an interactive child.
            guest.type('shell'); time.sleep(.3); expect_input(guest,'')
            guest.key('up'); expect_input(guest,''); guest.run('pwd','0p2:/home')
            guest.run('exit'); settled(guest,baseline)
            guest.key('up'); expect_input(guest,'shell'); guest.key('esc'); expect_input(guest,'')
            screen=guest.run('tree tree-check','4 directories, 4 files')
            expected=['tree-check','|-- alpha/','|   |-- a.txt','|   `-- empty/',
                      '|-- root.txt','|-- two words.txt','`-- zeta/','    `-- sub/',
                      '        `-- z.txt']
            for line in expected: assert line in screen,screen
            assert screen.index('alpha/')<screen.index('root.txt')<screen.index('zeta/')
            guest.run('tree tree-check/alpha/empty','0 directories, 0 files')
            guest.run('tree tree-check/root.txt','0 directories, 1 file')
            guest.run('tree tree-check/two words.txt','0 directories, 1 file')
            guest.run('tree absent','Path unavailable: absent')
            guest.run('tree ../home/tree-check','4 directories, 4 files')
            guest.run('cd tree-check'); guest.run('tree','4 directories, 4 files')
            guest.run('tree 1:/data','1 directory, 1 file'); guest.run('pwd','0p2:/home/tree-check')
            guest.run('tree sys:/home/deep','60 directories, 1 file')
            guest.run('cd sys:/home'); settled(guest,baseline)
            guest.run('tree sys:/bin/tree','0 directories, 1 file')
            guest.run('cls'); guest.run('tree tree-check','4 directories, 4 files')
            guest.key('up'); expect_input(guest,'tree tree-check')
            if not options.text: guest.screenshot(ROOT/'build/shell-history-tree.png')
            guest.key('esc'); expect_input(guest,''); settled(guest,baseline)
        except BaseException:
            print(guest.screen())
            print(guest.command('human-monitor-command',{'command-line':'info registers'})); raise
        finally: guest.close()
        tool('--check',image); tool('--check',extra)
    print('Shell history and tree passed ('+('UEFI/NVMe/USB' if options.uefi else 'VGA text' if options.text else 'BIOS graphics')+').',flush=True)

if __name__=='__main__': main()
