#!/usr/bin/env python3
"""Exercise the guest through QMP, using a disposable copy of its disk."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]

class Guest:
    def __init__(self, image, scratch, extra=None):
        self.scratch = Path(scratch)
        args = ["qemu-system-i386", "-m", "64M", "-display", "none", "-serial", "none",
                "-no-reboot", "-drive", f"format=raw,file={image},index=0", "-qmp", "stdio"]
        if extra:
            args += ["-drive", f"format=raw,file={extra},index=1"]
        self.log = (self.scratch / "qemu.log").open("w+")
        self.process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=self.log, text=True, env={**os.environ, "TMPDIR": scratch})
        greeting = self.process.stdout.readline()
        if not greeting:
            self.log.seek(0)
            raise RuntimeError(self.log.read())
        self.command("qmp_capabilities")

    def command(self, name, arguments=None):
        self.process.stdin.write(json.dumps({"execute": name, "arguments": arguments or {}}) + "\n")
        self.process.stdin.flush()
        while True:
            line = self.process.stdout.readline()
            if not line:
                raise RuntimeError("QEMU stopped unexpectedly")
            reply = json.loads(line)
            if "error" in reply:
                raise RuntimeError(reply)
            if "return" in reply:
                return reply["return"]

    def screen(self):
        dump = self.scratch / "vga.bin"
        self.command("human-monitor-command", {"command-line": f'pmemsave 0xb8000 4000 "{dump}"'})
        chars = dump.read_bytes()[::2].replace(b"\x00", b" ").decode("cp437")
        return "\n".join(chars[i:i + 80] for i in range(0, 2000, 80))

    def heap_pointer(self):
        symbols = subprocess.check_output(["nm", str(ROOT / "build/bootloader/loader.elf")], text=True)
        address = next(int(line.split()[0], 16) for line in symbols.splitlines() if line.split()[-1] == "env")
        dump = self.scratch / "heap.bin"
        self.command("human-monitor-command", {"command-line": f'pmemsave 0x{address:x} 4 "{dump}"'})
        return struct.unpack("<I", dump.read_bytes())[0]

    def wait_for(self, text, timeout=20):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            screen = self.screen()
            if text in screen:
                return screen
            time.sleep(.1)
        raise AssertionError(f"Guest did not display {text!r}:\n{screen}")

    def key(self, *keys):
        self.command("send-key", {"keys": [{"type": "qcode", "data": k} for k in keys], "hold-time": 20})
        time.sleep(.05)

    def type(self, text, submit=True):
        for char in text:
            if char.isalpha() and char.islower(): self.key(char)
            elif char.isdigit(): self.key(char)
            elif char == " ": self.key("spc")
            elif char == ".": self.key("dot")
            elif char == "_": self.key("shift", "minus")
            elif char == "-": self.key("minus")
            elif char == ":": self.key("shift", "semicolon")
            elif char == "$": self.key("shift", "4")
            elif char == '"': self.key("shift", "2")
            elif char == "@": self.key("shift", "apostrophe")
            elif char == "~": self.key("shift", "backslash")
            else: raise ValueError(char)
        if submit:
            self.key("ret")

    def run(self, text, expected="0 -> exit status"):
        self.type(text)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            screen = self.screen()
            if text == "cls" and screen.startswith(expected) and screen.splitlines()[1].strip() == "IDE0>":
                print("PASS guest: cls", flush=True)
                return screen
            start = screen.rfind("> " + text)
            tail = screen[start + len(text) + 2:] if start >= 0 else ""
            if expected in tail and "IDE" in tail[tail.index(expected) + len(expected):]:
                print(f"PASS guest: {text}", flush=True)
                return screen
            time.sleep(.1)
        raise AssertionError(f"Command {text!r} did not finish with {expected!r}:\n{screen}")

    def close(self):
        try: self.command("quit")
        finally:
            self.process.wait(timeout=5)
            self.process.stdin.close(); self.process.stdout.close(); self.log.close()

def main():
    subprocess.run(["make", "build/tests/large.elf"], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    with tempfile.TemporaryDirectory(prefix="systemzero-qemu-") as scratch:
        image = Path(scratch) / "disk.bin"
        shutil.copyfile(ROOT / "os-image.bin", image)
        extra = Path(scratch) / "extra.bin"
        with extra.open("wb") as f: f.truncate(8 * 1024 * 1024)
        binary = Path(scratch) / "binary"
        data = bytes(range(256)) * 300 + b"binary-tail"
        binary.write_bytes(data)
        invalid = Path(scratch) / "invalid"
        invalid.write_bytes(b"\x7fELF" + bytes(48))
        writer = str(ROOT / "ESFS_raw_write")
        editable = Path(scratch) / "editable"
        editable.write_bytes(b"abc")
        for name, source in (("large", ROOT / "build/tests/large.elf"), ("binary", binary), ("invalid", invalid), ("editing", editable)):
            subprocess.run([writer, "--replace", str(image), name, str(source)], check=True)
        guest = Guest(image, scratch, extra)
        try:
            print(guest.wait_for("IDE0>"), flush=True)
            initial_heap = guest.heap_pointer()
            screen = guest.run("list")
            for name in ("chain", "copy", "list", "read", "manual.txt"):
                assert name in screen, screen
            guest.run("drives")
            guest.run("drive 3", "1 -> exit status")
            guest.run("touch smoke.txt")
            assert "smoke.txt" in guest.run("list")
            guest.run("del smoke.txt")
            assert "smoke.txt" not in guest.run("list")
            guest.run("del smoke.txt", "1 -> exit status")
            guest.run("read nonexistent", "1 -> exit status")
            guest.run("nonexistent", "2 -> exit status")
            guest.run("invalid", "2 -> exit status")
            for _ in range(3):
                screen = guest.run("large")
                assert "large-elf-ok" in screen, screen
            guest.run("copy 0:binary 0:copied")
            guest.run("copy 0:binary 0:copied", "1 -> exit status")
            guest.run("drive 1")
            guest.run("format")
            guest.run("list")
            guest.run("copy 0:binary 1:crossed")
            guest.run("drive 0")
            # Retained per-drive caches must see child writes, deletions, and block reuse.
            for _ in range(2):
                guest.run("copy 1:crossed 0:returned")
                guest.run("del returned")
                guest.run("drive 1")
                assert "crossed" in guest.run("list")
                guest.run("drive 0")
            guest.type("edit editing")
            guest.wait_for("Ctrl+S: save")
            guest.key("x"); guest.key("ctrl", "s")
            guest.wait_for("Saved.")
            guest.key("esc"); guest.wait_for("IDE0>")
            guest.type("hexedit editing")
            guest.wait_for("w: save")
            guest.key("c"); guest.wait_for("Byte value (hex)")
            guest.type("00")
            guest.key("w"); guest.wait_for("Saved.")
            guest.key("esc"); guest.wait_for("IDE0>")
            guest.type("view editing")
            guest.wait_for("PgUp/PgDn: scroll")
            guest.key("esc"); guest.wait_for("IDE0>")
            print("PASS guest: text editor, binary editor, viewer", flush=True)
            guest.run('chain $touch nested.txt$del nested.txt$"nested-ok"')
            assert "nested.txt" not in guest.run("list")
            for _ in range(10): guest.run("cls")
            guest.run("list")
            assert guest.heap_pointer() == initial_heap, "Program execution leaked or rewound persistent heap memory"
        finally:
            guest.close()
        subprocess.run([str(ROOT / "ESFS_raw_write"), "--check", str(image)], check=True)
        output = Path(scratch) / "extracted"
        subprocess.run([writer, "--extract", str(image), "copied", str(output)], check=True)
        assert output.read_bytes() == data, "Guest binary copy changed the payload"
        subprocess.run([writer, "--extract", str(extra), "crossed", str(output)], check=True)
        assert output.read_bytes() == data, "Guest cross-drive copy changed the payload"
        subprocess.run([writer, "--extract", str(image), "editing", str(output)], check=True)
        assert output.read_bytes() == b"\0abc", "Editor saved the wrong payload or size"
        formatted = Path(scratch) / "formatted-system.bin"
        shutil.copyfile(ROOT / "os-image.bin", formatted)
        guest = Guest(formatted, scratch)
        try:
            guest.wait_for("IDE0>")
            guest.run("format")
            guest.run("list", "2 -> exit status")
        finally:
            guest.close()
        subprocess.run([writer, "--check", str(formatted)], check=True)
        print("QEMU smoke checks passed; original disk was untouched.")

if __name__ == "__main__": main()
