#!/usr/bin/env python3
"""Exercise actual PS/2 and xHCI mouse reports through QMP on disposable disks."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
from qemu_smoke import Guest, ROOT, COLS, ROWS
from qemu_uefi import UefiGuest
from qemu_zedit import TextGuest, settled
from qemu_shell import expect_input, submit

class Mouse:
    def __init__(self,guest):
        self.g=guest
        self.address=struct.unpack('<I',guest.memory(guest.env_address+56,4))[0]
        assert self.address,'Input subsystem was not initialized'
    def position(self): return struct.unpack('<ii',self.g.memory(self.address,8))
    def events(self,*events):
        self.g.command('input-send-event',{'events':list(events)}); time.sleep(.12)
    def move(self,x,y):
        for _ in range(40):
            ox,oy=self.position()
            if (ox,oy)==(x,y): return
            dx=max(-100,min(100,x-ox)); dy=max(-100,min(100,y-oy))
            self.events({'type':'rel','data':{'axis':'x','value':dx}}, {'type':'rel','data':{'axis':'y','value':dy}})
        raise AssertionError(f'Mouse did not move to {(x,y)}: {self.position()}')
    def button(self,down,name='left'): self.events({'type':'btn','data':{'down':down,'button':name}})
    def cell(self,row,column):
        t=self.g.terminal_state() if self.g.terminal_address() else None
        self.move((t['origin_x'] if t else 0)+column*8,(t['origin_y'] if t else (0 if self.g.desktop_state()['position'] else 16))+row*(12 if t else 16)+4)
    def click(self,row,column): self.cell(row,column); self.button(True); self.button(False)
    def drag(self,row,col,endrow,endcol):
        self.cell(row,col); self.button(True); self.cell(endrow,endcol); self.button(False)
    def wheel(self,up=True): self.button(True,'wheel-up' if up else 'wheel-down'); self.button(False,'wheel-up' if up else 'wheel-down')
    def clipboard(self):
        length=struct.unpack('<I',self.g.memory(self.address+12,4))[0]
        return self.g.memory(self.address+44,length) if length else b''
    def shown(self):
        t=self.g.terminal_address()
        if not t: return self.g.screen()
        data=self.g.memory(t+COLS*ROWS*2,COLS*ROWS*2)[::2].replace(b'\0',b' ')
        return '\n'.join(data[i:i+COLS].decode('cp437') for i in range(0,len(data),COLS))

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--uefi',action='store_true'); parser.add_argument('--text',action='store_true')
    parser.add_argument('--image',type=Path,default=ROOT/'os-image.bin'); opt=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='systemzero-mouse-') as temp:
        scratch=Path(temp); image=scratch/'disk.bin'; shutil.copyfile(opt.image,image)
        payload=scratch/'payload'; source=b'alpha\tbeta\ngamma delta\n'+b''.join(f'line{i:03d}\n'.encode() for i in range(150)); payload.write_bytes(source)
        writer=str(ROOT/'ESFS_raw_write')
        subprocess.run([writer,'--replace',str(image),'home/mouse.txt',str(payload)],check=True,stdout=subprocess.DEVNULL)
        scroll=scratch/'scroll.sz'
        scroll.write_text('int main(){int i=0;while(i<150){print("scroll line ");print_dec(i);newline();i++;}sleep(2000);return 0;}')
        subprocess.run([writer,'--replace',str(image),'home/scroll.sz',str(scroll)],check=True,stdout=subprocess.DEVNULL)
        guest=UefiGuest(image,scratch,'nvme',True) if opt.uefi else TextGuest(image,temp,vga='cirrus') if opt.text else Guest(image,temp)
        try:
            guest.wait_for('DISK0p2/home>'); time.sleep(.2); base=guest.heap_pointer(); mouse=Mouse(guest)
            mouse.move(0,0)
            if not opt.text:
                t=guest.terminal_state(); address=t['pixels']+100*t['pitch']*4
                before=guest.memory(address,100*t['pitch']*4)
                mouse.move(120,130); mouse.move(0,0)
                assert guest.memory(address,100*t['pitch']*4)==before,'Pointer left framebuffer damage'
            mouse.move(100,100); mouse.move(0,0); mouse.move(639 if opt.text else 799,399 if opt.text else 599)
            guest.run('cls'); guest.type('set NOTE ac',submit=False)
            # Place the command caret between a and c.
            mouse.click(0,len('DISK0p2/home> ')+len('set NOTE a'))
            guest.type('b',submit=False); expect_input(guest,'set NOTE abc'); submit(guest,'set NOTE abc','Saved NOTE')
            guest.type('set NOTE ',submit=False); time.sleep(.2)
            # Copy from the previous command and paste at the current caret.
            mouse.drag(0,len('DISK0p2/home> ')+len('set NOTE '),0,len('DISK0p2/home> ')+len('set NOTE abc'))
            guest.key('ctrl','c'); assert mouse.clipboard()==b'abc',mouse.clipboard()
            guest.key('ctrl','v'); expect_input(guest,'set NOTE abc'); submit(guest,'set NOTE abc','Saved NOTE')
            guest.type('edit mouse.txt')
            guest.wait_for('Ctrl+S: save')
            mouse.click(1,6); guest.type('X',submit=False)
            assert guest.screen().splitlines()[1].startswith('gamma Xdelta'),guest.screen()
            mouse.drag(0,0,1,5); guest.key('ctrl','c'); assert mouse.clipboard()==b'alpha\tbeta\ngamma',mouse.clipboard()
            guest.key('ctrl','v') # Replaces the selected range with its original contents.
            guest.key('end'); guest.key('ctrl','v'); guest.key('ctrl','s')
            mouse.wheel(); assert guest.screen().splitlines()[0].startswith('alpha'),guest.screen()
            mouse.wheel(False); assert guest.screen().splitlines()[0].startswith('line'),guest.screen()
            guest.key('esc'); settled(guest,base); assert mouse.clipboard()==b'alpha\tbeta\ngamma'
            guest.type('view mouse.txt'); guest.wait_for('PgUp/PgDn: scroll'); mouse.wheel(False)
            assert guest.screen().splitlines()[0].startswith('line'),guest.screen()
            mouse.drag(0,0,0,7); guest.key('ctrl','c'); assert mouse.clipboard().startswith(b'line'),mouse.clipboard()
            guest.key('esc'); settled(guest,base)
            guest.type('zedit mouse.txt'); guest.wait_for('zedit |')
            mouse.click(1,6); guest.type('Z',submit=False); assert guest.screen().splitlines()[1][6:].startswith('Zgamma'),guest.screen()
            mouse.drag(0,6,0,11); guest.key('ctrl','c'); assert mouse.clipboard()==b'alpha',mouse.clipboard()
            guest.key('ctrl','v'); guest.key('esc'); settled(guest,base)
            guest.type('hexedit mouse.txt'); guest.wait_for('HEX mouse.txt')
            mouse.click(1,13); guest.key('c'); guest.type('42')
            mouse.drag(1,60,1,63); guest.key('ctrl','c'); assert mouse.clipboard()==b'aBp',mouse.clipboard()
            guest.key('ctrl','v'); mouse.wheel(False)
            assert guest.screen().splitlines()[1].startswith('00000030'),guest.screen()
            mouse.wheel(); assert guest.screen().splitlines()[1].startswith('00000000'),guest.screen()
            guest.key('ctrl','s'); guest.key('esc'); settled(guest,base)
            guest.run('cc scroll.sz scrolling','Compiled')
            guest.type('./scrolling'); guest.wait_for('scroll line 149')
            assert guest.desktop_state()['command']=='./scrolling'
            live=mouse.shown(); mouse.wheel()
            assert mouse.shown()!=live,'History did not scroll while foreground command was running'
            assert guest.desktop_state()['command']=='./scrolling'
            mouse.wheel(False)
            deadline=time.monotonic()+5
            while guest.desktop_state()['command']!='shell' and time.monotonic()<deadline: time.sleep(.05)
            settled(guest,base)
            # Scroll live terminal output back and forward. Navigation must not edit input.
            guest.run('cls')
            for _ in range(65): guest.key('ret')
            guest.type('draft',submit=False); live=mouse.shown(); mouse.wheel(); older=mouse.shown()
            assert older!=live,'Wheel did not scroll terminal history'
            mouse.wheel(False); assert mouse.shown()==live,'Scroll did not return to live terminal'
            guest.type('x',submit=False); expect_input(guest,'draftx'); guest.key('esc'); settled(guest,base)
            # Copy across a soft-wrapped command without inserting line breaks.
            guest.run('cls'); command='set NOTE '+('a'*130)
            guest.type(command,submit=False); expect_input(guest,command)
            width=80 if opt.text else COLS; prompt=len('DISK0p2/home> ')
            mouse.drag(0,prompt+9,1,prompt+len(command)-width)
            guest.key('ctrl','c'); assert mouse.clipboard()==b'a'*130,mouse.clipboard()
            guest.type('b',submit=False); expect_input(guest,'set NOTE b')
            guest.key('esc'); settled(guest,base)
            guest.run('set STATUS_POSITION bottom','Saved')
            guest.run('cls'); guest.type('set NOTE xy',submit=False)
            mouse.click(0,prompt+len('set NOTE x')); guest.type('z',submit=False)
            expect_input(guest,'set NOTE xzy'); guest.key('esc'); settled(guest,base)
            guest.run('set STATUS_POSITION top','Saved')
            if not opt.text: guest.screenshot(ROOT/('build/mouse-usb.png' if opt.uefi else 'build/mouse-ps2.png'))
        except BaseException:
            print(guest.screen()); print('mouse:',mouse.position(), 'clipboard:',mouse.clipboard()); raise
        finally: guest.close()
        subprocess.run([writer,'--extract',str(image),'home/mouse.txt',str(payload)],check=True,stdout=subprocess.DEVNULL)
        expected=b'aBpha\tbeta\ngamma Xdeltaalpha\tbeta\ngamma\n'+source.split(b'\n',2)[2]
        assert payload.read_bytes()==expected,(payload.read_bytes()[:100],expected[:100])
    print('Mouse integration passed ('+('UEFI USB' if opt.uefi else 'VGA PS/2' if opt.text else 'BIOS PS/2')+').',flush=True)
if __name__=='__main__': main()
