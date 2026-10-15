#!/usr/bin/env python3
"""Keep the pointer responsive during text batches and sustained drag traffic."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
from qemu_smoke import Guest,ROOT
from qemu_uefi import UefiGuest
from qemu_zedit import TextGuest,settled
from qemu_mouse import Mouse

def wait(check,description,timeout=2):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        if check(): return
        time.sleep(.01)
    raise AssertionError(description)

class DragMouse(Mouse):
    def raw(self,dx=0,dy=0,down=None):
        events=[]
        if dx: events.append({'type':'rel','data':{'axis':'x','value':dx}})
        if dy: events.append({'type':'rel','data':{'axis':'y','value':dy}})
        if down is not None: events.append({'type':'btn','data':{'button':'left','down':down}})
        if events: self.g.command('input-send-event',{'events':events})
    def visible(self,x,y):
        t=self.g.terminal_address()
        if t:
            state=self.g.terminal_state(); pixels=state['pixels']; pitch=state['pitch']
            tip=struct.unpack('<I',self.g.memory(pixels+(y*pitch+x)*4,4))[0]
            body=struct.unpack('<I',self.g.memory(pixels+((y+4)*pitch+x+1)*4,4))[0]
            return tip==0 and body==0xffffff
        cell=struct.unpack('<H',self.g.memory(0xb8000+((y//16)*80+x//8)*2,2))[0]
        return cell>>8==0x70
    def arrive(self,x,y):
        self.move(x,y)
        wait(lambda:self.visible(x,y),f'Pointer stopped drawing at {(x,y)}, input position={self.position()}')

def main():
    p=argparse.ArgumentParser(); p.add_argument('--uefi',action='store_true'); p.add_argument('--text',action='store_true')
    p.add_argument('--image',type=Path,default=ROOT/'os-image.bin'); opt=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='systemzero-mouse-drag-') as temp:
        scratch=Path(temp); image=scratch/'disk.bin'; shutil.copyfile(opt.image,image)
        source=scratch/'pointer.sz'; source.write_text('int main(){begin_update();put_text(3,0,"pending text batch",15);sleep(6000);end_update();return 0;}')
        data=scratch/'editing'; data.write_text(''.join(f'line {i:03d}: select this text and release the button\n' for i in range(100)))
        for name,path in (('home/pointer.sz',source),('home/drag.txt',data)):
            subprocess.run([str(ROOT/'ESFS_raw_write'),'--replace',str(image),name,str(path)],check=True,stdout=subprocess.DEVNULL)
        g=UefiGuest(image,scratch,'nvme',True) if opt.uefi else TextGuest(image,temp,vga='cirrus') if opt.text else Guest(image,temp)
        try:
            g.wait_for('DISK0p2/home>'); baseline=g.heap_pointer(); m=DragMouse(g)
            g.run('cc pointer.sz mouse-frame','Compiled')
            g.type('./mouse-frame')
            wait(lambda:g.desktop_state()['command']=='./mouse-frame','Batched update program did not start')
            time.sleep(.1)
            if g.terminal_address(): assert g.terminal_state()['updates']>0
            m.arrive(400,200); m.arrive(520,280); m.arrive(400,200)
            # Text batching must defer text, without suspending the mouse.
            assert g.desktop_state()['command']=='./mouse-frame','Pointer did not move until after the text batch ended'
            if g.terminal_address(): assert g.terminal_state()['updates']>0
            wait(lambda:g.desktop_state()['command']=='shell','Batch program did not return',10); settled(g,baseline)
            for command,title in ((None,None),('edit drag.txt','Ctrl+S: save'),('zedit drag.txt','F5 Run'),('view drag.txt','PgUp/PgDn: scroll')):
                if command: g.type(command); g.wait_for(title)
                m.cell(2,10); m.button(True)
                # High-rate input deliberately omits the usual 120 ms wait.
                # It crosses many xHCI transfer/event ring wraps on USB.
                clock=g.desktop_state()
                clock_start=clock['seconds']*1000+clock['milliseconds']
                start=time.monotonic()
                for i in range(1600):
                    m.raw(1 if (i//300)%2==0 else -1,1 if (i//150)%2==0 else -1)
                    time.sleep(.001)
                clock=g.desktop_state()
                clock_end=clock['seconds']*1000+clock['milliseconds']
                assert (clock_end-clock_start)%86400000>=1000,'Drag starved timer interrupts'
                m.raw(down=False)
                wait(lambda:g.memory(m.address+8,1)==b'\0','Mouse release was not processed')
                m.arrive(400,200); m.arrive(500,220)
                # A released drag must not resume merely by moving the pointer.
                g.key('ctrl','c'); selection=m.clipboard(); m.arrive(450,250); g.key('ctrl','c')
                assert m.clipboard()==selection,'Selection followed motion after releasing the button'
                print(f'PASS drag {command or "console"}: {time.monotonic()-start:.2f}s',flush=True)
                if command: g.key('esc'); settled(g,baseline)
            g.run('pwd','0p2:/home')
        except BaseException:
            print(g.screen()); raise
        finally:g.close()
    print('Pointer batching and fast drag passed ('+('UEFI USB' if opt.uefi else 'VGA PS/2' if opt.text else 'BIOS PS/2')+').',flush=True)
if __name__=='__main__': main()
