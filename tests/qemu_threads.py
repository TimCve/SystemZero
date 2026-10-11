#!/usr/bin/env python3
"""Check scheduling, the status bar, and saved graphical settings on disposable disks."""
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import time
from qemu_smoke import Guest, ROOT

def wait(predicate, description, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result: return result
        time.sleep(.03)
    raise AssertionError(description)

def main():
    with tempfile.TemporaryDirectory(prefix="systemzero-threads-qemu-") as scratch:
        image = Path(scratch) / "disk.bin"; shutil.copyfile(ROOT / "os-image.bin", image)
        writer = str(ROOT / "ESFS_raw_write")
        # These checks start with the default settings even if the user's disk has a saved preference.
        subprocess.run([writer, "--delete", str(image), "systemzero.cfg"], check=True)
        for name, source in (("thread-test.sz", "thread_language.sz"), ("auto.sz", "thread_auto.sz"), ("hold.sz", "thread_hold.sz")):
            subprocess.run([writer, "--replace", str(image), name, str(ROOT / "tests" / source)], check=True)
        guest = Guest(image, scratch)
        try:
            guest.wait_for("IDE0>"); initial_heap = guest.heap_pointer()
            state = guest.desktop_state(); assert state["position"] == 0 and state["color"] == 31
            assert re.search(r"SystemZero  v\d+\.\d+\.\d+  shell", state["bar"])
            assert re.search(r"\d\d:\d\d:\d\d", state["bar"])
            guest.run("cc thread-test.sz thtest"); guest.run("thtest", "thread-checks-ok")
            guest.run("thtest", "thread-checks-ok")
            guest.run("cc auto.sz auto"); guest.run("auto", "auto-joined")
            guest.run("cc threads.sz threads"); guest.run("threads", "Total: 300")
            assert guest.heap_pointer() == initial_heap, "Thread stacks or worker allocations leaked command memory"
            guest.run("cc hold.sz hold")
            guest.type("hold alpha beta")
            state = guest.desktop_state()
            assert state["command"] == "hold alpha beta" and "hold alpha beta" in state["bar"]
            started = state["seconds"]
            wait(lambda: guest.desktop_state()["seconds"] != started, "Clock stopped during CPU-bound code")
            guest.wait_for("busy-clock-ok")
            wait(lambda: guest.desktop_state()["command"] == "shell", "Command title did not restore")
            guest.type("settings"); guest.wait_for("SystemZero Settings")
            started = guest.desktop_state()["seconds"]
            wait(lambda: guest.desktop_state()["seconds"] != started, "Clock stopped while settings waited for input")
            guest.key("right"); guest.key("tab"); guest.key("right"); guest.key("esc")
            guest.wait_for("IDE0>")
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
            guest.screenshot(ROOT / "build/settings-preview.png")
            guest.key("ret")
            wait(lambda: guest.desktop_state()["position"] == 1 and guest.desktop_state()["color"] == 2,
                 "Settings did not apply")
            guest.wait_for("IDE0>")
            assert (guest.desktop_state()["seconds"] - (12 * 3600 + 34 * 60 + 56)) % 86400 < 5
            terminal = guest.terminal_state()
            assert terminal["color"] == 2
            assert struct.unpack("<I", guest.memory(terminal["pixels"], 4))[0] == 0
            assert struct.unpack("<I", guest.memory(terminal["pixels"] + (terminal["height"] - 12) * terminal["pitch"] * 4, 4))[0] == 0x333333
            guest.screenshot(ROOT / "build/status-bottom.png")
            assert guest.heap_pointer() == initial_heap
            saved_seconds = guest.desktop_state()["seconds"]
        except BaseException:
            print(guest.screen()); print(guest.command("human-monitor-command", {"command-line": "info registers"})); raise
        finally: guest.close()
        restored = Path(scratch) / "extracted"
        subprocess.run([writer, "--extract", str(image), "auto-join.txt", str(restored)], check=True)
        assert restored.read_bytes() == struct.pack("<I", 77)
        subprocess.run([writer, "--extract", str(image), "systemzero.cfg", str(restored)], check=True)
        config = restored.read_bytes(); assert len(config) == 16
        for vga in ("std", "cirrus"):
            guest = Guest(image, scratch, vga=vga)
            try:
                guest.wait_for("IDE0>")
                state = guest.desktop_state(); assert state["position"] == 1 and state["color"] == 2
                assert (state["seconds"] - saved_seconds) % 86400 < 20
                if guest.terminal_address():
                    assert guest.terminal_state()["color"] == 2
                    assert guest.memory(guest.terminal_address(), 2)[1] == 2
                else:
                    assert "SystemZero" in guest.memory(0xb8000 + 24 * 160, 160)[::2].decode()
                guest.type("settings"); guest.wait_for("SystemZero Settings")
                guest.key("esc"); guest.wait_for("IDE0>")
                print(f"PASS {vga}: settings and clock persisted; viewer geometry preserved", flush=True)
            finally: guest.close()
        # Invalid settings are ignored rather than corrupting the console or clock.
        restored.write_bytes(config[:-1] + bytes([config[-1] ^ 1]))
        subprocess.run([writer, "--replace", str(image), "systemzero.cfg", str(restored)], check=True)
        guest = Guest(image, scratch)
        try:
            guest.wait_for("IDE0>")
            assert guest.desktop_state()["position"] == 0 and guest.desktop_state()["color"] == 31
        finally: guest.close()
        subprocess.run([writer, "--check", str(image)], check=True)
        print("Preemptive threads, mutexes, command cleanup, status bar, clock, and graphical settings passed.")

if __name__ == "__main__": main()
