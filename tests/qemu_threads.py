#!/usr/bin/env python3
"""Check scheduling, the status bar, and saved graphical settings on disposable disks."""
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import time
from qemu_smoke import Guest, ROOT, COLS

def wait(predicate, description, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result: return result
        time.sleep(.03)
    raise AssertionError(description)

def secondary_cells(guest, color, secondary):
    address=guest.terminal_address()
    columns=COLS if address else 80
    state=guest.desktop_state()
    base=address or (0xb8000 + (160 if not state['position'] else 0))
    cells=guest.memory(base,columns*(48 if address else 24)*2)
    lines=guest.screen().splitlines()
    for row,line in enumerate(lines):
        if '|   __|_ _' in line or line.strip()=='DISK0p2>':
            col=line.index('|') if '|   __|_ _' in line else line.index('D')
            assert cells[(row*columns+col)*2+1]==(color&0xf0)|secondary,(line,secondary)
    assert cells[1]==color, 'Primary text changed with the secondary color'

def main(source_image=ROOT / "os-image.bin"):
    with tempfile.TemporaryDirectory(prefix="systemzero-threads-qemu-") as scratch:
        image = Path(scratch) / "disk.bin"; shutil.copyfile(source_image, image)
        writer = str(ROOT / "ESFS_raw_write")
        # These checks start with the default settings even if the user's disk has a saved preference.
        subprocess.run([writer, "--delete", str(image), "systemzero.cfg"], check=True)
        subprocess.run([writer, "--delete", str(image), "sys/config/environment"], check=True)
        for name, source in (("thread-test.sz", "thread_language.sz"), ("auto.sz", "thread_auto.sz"), ("hold.sz", "thread_hold.sz")):
            subprocess.run([writer, "--replace", str(image), name, str(ROOT / "tests" / source)], check=True)
        guest = Guest(image, scratch)
        try:
            guest.boot_root(); initial_heap = guest.heap_pointer()
            state = guest.desktop_state(); assert state["position"] == 0 and state["color"] == 31
            assert state['secondary_color']==14
            secondary_cells(guest,31,14)
            assert re.search(r"SystemZero  v\d+\.\d+\.\d+  shell", state["bar"])
            assert re.search(r"\d\d:\d\d:\d\d", state["bar"])
            guest.run("cc thread-test.sz thtest"); guest.run("./thtest", "thread-checks-ok")
            guest.run("./thtest", "thread-checks-ok")
            guest.run("cc auto.sz auto"); guest.run("./auto", "auto-joined")
            guest.run("cc sys:/home/examples/threads.sz threads"); guest.run("./threads", "Total: 300")
            assert guest.heap_pointer() == initial_heap, "Thread stacks or worker allocations leaked command memory"
            guest.run("cc hold.sz hold")
            guest.type("./hold alpha beta")
            wait(lambda: guest.desktop_state()["command"] == "./hold alpha beta", "Busy command did not start")
            state = guest.desktop_state()
            assert state["command"] == "./hold alpha beta" and "hold alpha beta" in state["bar"]
            started = state["seconds"]
            wait(lambda: guest.desktop_state()["seconds"] != started, "Clock stopped during CPU-bound code")
            guest.wait_for("busy-clock-ok")
            wait(lambda: guest.desktop_state()["command"] == "shell", "Command title did not restore")
            guest.type("settings"); guest.wait_for("SystemZero Settings")
            started = guest.desktop_state()["seconds"]
            wait(lambda: guest.desktop_state()["seconds"] != started, "Clock stopped while settings waited for input")
            guest.key("right"); guest.key("tab"); guest.key("right"); guest.key("esc")
            guest.boot_root()
            assert guest.desktop_state()["position"] == 0 and guest.desktop_state()["color"] == 31
            guest.type("settings"); guest.wait_for("SystemZero Settings")
            guest.type("12", submit=False); guest.key("tab")
            guest.type("34", submit=False); guest.key("tab")
            guest.type("56", submit=False); guest.key("tab")
            guest.key("right"); guest.key("tab")
            before = guest.terminal_state()["rendered"]
            for _ in range(3): guest.key("right")
            assert guest.terminal_state()["rendered"] - before < 150, "Color selection repainted the entire settings screen"
            guest.key("tab"); guest.key("left")
            guest.key('tab'); guest.key('up'); guest.key('up')
            guest.screenshot(ROOT / "build/settings-preview.png")
            guest.key("ret")
            wait(lambda: guest.desktop_state()["position"] == 1 and guest.desktop_state()["color"] == 2,
                 "Settings did not apply")
            assert guest.desktop_state()['secondary_color']==6
            guest.wait_for("DISK0p2>")
            assert (guest.desktop_state()["seconds"] - (12 * 3600 + 34 * 60 + 56)) % 86400 < 5
            terminal = guest.terminal_state()
            assert terminal["color"] == 2
            assert struct.unpack("<I", guest.memory(terminal["pixels"], 4))[0] == 0
            assert struct.unpack("<I", guest.memory(terminal["pixels"] + (terminal["height"] - 12) * terminal["pitch"] * 4, 4))[0] == 0x333333
            guest.screenshot(ROOT / "build/status-bottom.png")
            wait(lambda: guest.heap_pointer()==initial_heap,'Settings command leaked heap')
            saved_seconds = guest.desktop_state()["seconds"]
        except BaseException:
            print(guest.screen()); print(guest.command("human-monitor-command", {"command-line": "info registers"})); raise
        finally: guest.close()
        restored = Path(scratch) / "extracted"
        subprocess.run([writer, "--extract", str(image), "auto-join.txt", str(restored)], check=True)
        assert restored.read_bytes() == struct.pack("<I", 77)
        subprocess.run([writer, "--extract", str(image), "sys/config/environment", str(restored)], check=True)
        config = restored.read_bytes(); assert b"COLOR_SECONDARY=6\n" in config
        expected_secondary=6
        for vga in ("std", "cirrus"):
            guest = Guest(image, scratch, vga=vga)
            try:
                guest.boot_root()
                state = guest.desktop_state(); assert state["position"] == 1 and state["color"] == 2
                assert state['secondary_color']==expected_secondary
                secondary_cells(guest,2,expected_secondary)
                assert (state["seconds"] - saved_seconds) % 86400 < 20
                if guest.terminal_address():
                    assert guest.terminal_state()["color"] == 2
                    assert guest.memory(guest.terminal_address(), 2)[1] == 2
                else:
                    assert "SystemZero" in guest.memory(0xb8000 + 24 * 160, 160)[::2].decode()
                guest.type("settings"); guest.wait_for("SystemZero Settings")
                guest.key("esc"); guest.wait_for("DISK0p2>")
                if vga=='std':
                    # Keep semantic roles even when both text colors temporarily match.
                    for adjustment,result in ((('up',),2),(('down','down'),10)):
                        guest.type('settings'); guest.wait_for('Secondary text color')
                        for _ in range(6): guest.key('tab')
                        for key in adjustment: guest.key(key)
                        guest.key('ret')
                        wait(lambda: guest.desktop_state()['secondary_color']==result,'Secondary color did not apply')
                        wait(lambda: guest.heap_pointer()==initial_heap,'Settings cleanup did not complete')
                        secondary_cells(guest,2,result)
                    expected_secondary=10
                    saved_seconds=guest.desktop_state()['seconds']
                print(f"PASS {vga}: settings and clock persisted; viewer geometry preserved", flush=True)
            finally: guest.close()
        # Invalid settings are ignored rather than corrupting the console or clock.
        restored.write_bytes(config.replace(b"COLOR_BACKGROUND=0\n", b"COLOR_BACKGROUND=2\n"))
        subprocess.run([writer, "--replace", str(image), "sys/config/environment", str(restored)], check=True)
        guest = Guest(image, scratch)
        try:
            guest.boot_root()
            assert guest.desktop_state()["position"] == 0 and guest.desktop_state()["color"] == 31
        finally: guest.close()
        # The original 16-byte preference record must still load with its defaults.
        subprocess.run([writer,'--delete',str(image),'sys/config/environment'],check=True)
        offset=int(next(line.split(b'=')[1] for line in config.splitlines() if line.startswith(b'CLOCK_OFFSET=')))
        magic,position,color=0x535a4331,1,2
        check=magic^offset^position^(color<<8)^0x18b52c4d
        restored.write_bytes(struct.pack('<IIBBHI',magic,offset,position,color,0,check))
        subprocess.run([writer,'--replace',str(image),'systemzero.cfg',str(restored)],check=True)
        guest=Guest(image,scratch)
        try:
            guest.boot_root()
            state=guest.desktop_state()
            assert (state['position'],state['color'],state['secondary_color'])==(1,2,14)
            secondary_cells(guest,2,14)
        finally: guest.close()
        subprocess.run([writer, "--check", str(image)], check=True)
        print("Preemptive threads, mutexes, command cleanup, status bar, clock, and graphical settings passed.")

if __name__ == "__main__": main()
