#!/usr/bin/env python3
"""Exercise the guest through QMP, using a disposable copy of its disk."""
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import struct
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
def display_defines(path):
    return {name: int(value) for name, value in
            re.findall(r"^#define (\w+) (\d+)u$", (ROOT / path).read_text(), re.MULTILINE)}

TERMINAL = display_defines("shared/terminal.h")
VIDEO = display_defines("kernel/drivers/io/video.h")
COLS, ROWS = TERMINAL["TERM_COLS"], TERMINAL["TERM_ROWS"]
FONT_HEIGHT, CELL_HEIGHT = TERMINAL["TERM_FONT_HEIGHT"], TERMINAL["TERM_CELL_HEIGHT"]
FONT_TOP = TERMINAL["TERM_FONT_TOP"]

class Guest:
    def __init__(self, image, scratch, extra=None, vga="std"):
        self.scratch = Path(scratch)
        args = ["qemu-system-i386", "-m", "64M", "-vga", vga, "-display", "none", "-serial", "none",
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
        symbols = subprocess.check_output(["nm", str(ROOT / "build/bootloader/loader.elf")], text=True)
        self.env_address = next(int(line.split()[0], 16) for line in symbols.splitlines() if line.split()[-1] == "env")

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

    def memory(self, address, size):
        dump = self.scratch / "memory.bin"
        self.command("human-monitor-command", {"command-line": f'pmemsave 0x{address:x} {size} "{dump}"'})
        return dump.read_bytes()

    def terminal_address(self):
        return struct.unpack("<I", self.memory(self.env_address + 20, 4))[0]

    def screen(self):
        address = self.terminal_address()
        columns, rows = (COLS, ROWS) if address else (80, 25)
        chars = self.memory(address or 0xb8000, columns * rows * 2)[::2].replace(b"\x00", b" ").decode("cp437")
        return "\n".join(chars[i:i + columns] for i in range(0, columns * rows, columns))

    def screenshot(self, path):
        self.command("screendump", {"filename": str(Path(path).resolve()), "format": "png"})

    def terminal_state(self):
        address = self.terminal_address()
        assert address, "Framebuffer terminal was not initialized"
        values = struct.unpack("<12I4B", self.memory(address + COLS * ROWS * 4, 52))
        return dict(zip(("pixels", "font", "width", "height", "pitch", "origin_x", "origin_y",
                         "cursor", "shown_cursor", "updates", "rendered", "presentations",
                         "color", "scrolling", "visible", "clear_canvas"), values))

    def heap_pointer(self):
        return struct.unpack("<I", self.memory(self.env_address, 4))[0]

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
            elif char == "\n": self.key("ret")
            elif char == "\t": self.key("tab")
            elif char == "(": self.key("shift", "9")
            elif char == ")": self.key("shift", "0")
            elif char == "{": self.key("shift", "bracket_left")
            elif char == "}": self.key("shift", "bracket_right")
            elif char == "[": self.key("bracket_left")
            elif char == "]": self.key("bracket_right")
            elif char == ";": self.key("semicolon")
            elif char == "=": self.key("equal")
            elif char == "+": self.key("shift", "equal")
            elif char == "*": self.key("shift", "8")
            elif char == "/": self.key("slash")
            elif char == "<": self.key("shift", "comma")
            elif char == ">": self.key("shift", "dot")
            elif char == "!": self.key("shift", "1")
            elif char == ",": self.key("comma")
            elif char == "\\": self.key("less")
            elif char == "'": self.key("apostrophe")
            else: raise ValueError(char)
        if submit:
            self.key("ret")

    def run(self, text, expected="0 -> exit status"):
        self.type(text)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            screen = self.screen()
            state = self.terminal_state()
            row = state["cursor"] // COLS
            lines = screen.splitlines()
            start = screen.rfind("> " + text)
            tail = screen[start + len(text) + 2:] if start >= 0 else ""
            # Long output can scroll the command echo away; the live cursor identifies
            # the new shell prompt, with this command's status immediately above it.
            if 0 < row < ROWS and state["cursor"] % COLS == 6 and \
                    lines[row].strip() in ("IDE0>", "IDE1>", "IDE2>", "IDE3>") and \
                    lines[row - 1].strip() == expected:
                print(f"PASS guest: {text}", flush=True)
                # A taller screen retains previous listings; return this command's output.
                return tail if start >= 0 else screen
            if text == "cls" and screen.startswith(expected) and screen.splitlines()[1].strip() == "IDE0>":
                print("PASS guest: cls", flush=True)
                return screen
            if expected in tail and "IDE" in tail[tail.index(expected) + len(expected):]:
                print(f"PASS guest: {text}", flush=True)
                return tail
            time.sleep(.1)
        raise AssertionError(f"Command {text!r} did not finish with {expected!r}:\n{screen}")

    def close(self):
        try: self.command("quit")
        finally:
            self.process.wait(timeout=5)
            self.process.stdin.close(); self.process.stdout.close(); self.log.close()

def main():
    subprocess.run(["make", "build/tests/large.elf", "build/tests/nested.elf"], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
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
        multiline = Path(scratch) / "multiline"
        multiline.write_bytes(b"".join(f"line{i:02d}\n".encode() for i in range(ROWS * 2)))
        wrapping = Path(scratch) / "wrapping"
        wrapping.write_bytes(b"a" * (COLS - 1) + b"bcd")
        for name, source in (("large", ROOT / "build/tests/large.elf"), ("binary", binary),
                             ("nested", ROOT / "build/tests/nested.elf"),
                             ("invalid", invalid), ("editing", editable), ("multiline", multiline),
                             ("wrapping", wrapping)):
            subprocess.run([writer, "--replace", str(image), name, str(source)], check=True)
        guest = Guest(image, scratch, extra)
        try:
            print(guest.wait_for("IDE0>"), flush=True)
            state = guest.terminal_state()
            assert (state["width"], state["height"], state["pitch"]) == (
                VIDEO["VIDEO_WIDTH"], VIDEO["VIDEO_HEIGHT"], VIDEO["VIDEO_WIDTH"])
            assert any(guest.memory(state["font"], 256 * FONT_HEIGHT)), "BIOS font was not preserved"
            guest.screenshot(ROOT / "build/graphics-shell.png")
            assert struct.unpack(">II", (ROOT / "build/graphics-shell.png").read_bytes()[16:24]) == (
                VIDEO["VIDEO_WIDTH"], VIDEO["VIDEO_HEIGHT"])
            cell = guest.memory(guest.terminal_address(), 2)
            assert cell == b"K\x1f"
            glyph = guest.memory(state["font"] + ord("K") * FONT_HEIGHT, FONT_HEIGHT)
            origin = state["origin_y"] * state["pitch"] + state["origin_x"]
            pixels = guest.memory(state["pixels"] + origin * 4, CELL_HEIGHT * state["pitch"] * 4)
            for y in range(CELL_HEIGHT):
                for x in range(8):
                    value = struct.unpack_from("<I", pixels, (y * state["pitch"] + x) * 4)[0]
                    ink = FONT_TOP <= y < FONT_TOP + FONT_HEIGHT and glyph[y - FONT_TOP] & (0x80 >> x)
                    assert value == (0xffffff if ink else 0x0000aa)
            initial_heap = guest.heap_pointer()
            screen = guest.run("list")
            for name in ("copy", "list", "read", "manual.txt"):
                assert name in screen, screen
            assert "chain" not in screen, screen
            guest.run("chain", "2 -> exit status")
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
            assert "Ctrl+S: save" in guest.screen().splitlines()[ROWS - 1]
            before = guest.terminal_state()["rendered"]
            guest.key("shift")
            assert guest.terminal_state()["rendered"] == before, "Modifier keys repainted the editor"
            guest.key("x"); guest.wait_for("Modified")
            assert guest.screen().startswith("xabc")
            assert guest.terminal_state()["rendered"] - before < 32, "Typing repainted the whole display"
            guest.screenshot(ROOT / "build/graphics-editor.png")
            guest.key("ctrl", "s")
            guest.wait_for("Saved.")
            guest.key("esc"); guest.wait_for("IDE0>")
            guest.type("hexedit editing")
            guest.wait_for("w: save")
            before = guest.terminal_state()["rendered"]
            guest.key("right"); guest.key("left")
            assert guest.terminal_state()["rendered"] - before <= 12, "Hex cursor movement repainted the whole display"
            guest.key("c"); guest.wait_for("Byte value (hex)"); guest.key("esc")
            guest.wait_for("Byte entry cancelled")
            guest.key("c"); guest.wait_for("Byte value (hex)")
            guest.type("00")
            guest.key("w"); guest.wait_for("Saved.")
            guest.screenshot(ROOT / "build/graphics-hex-editor.png")
            guest.key("esc"); guest.wait_for("IDE0>")
            guest.type("edit multiline"); guest.wait_for("Ctrl+S: save")
            guest.key("pgdn")
            page_start = f"line{ROWS - 1:02d}"
            assert guest.wait_for(page_start).startswith(page_start), "Page Down did not move a full page"
            guest.key("pgup")
            assert guest.wait_for("line00").startswith("line00")
            guest.key("end"); guest.key("down"); guest.key("z"); guest.key("ctrl", "s")
            guest.wait_for("Saved."); guest.key("esc"); guest.wait_for("IDE0>")
            guest.type("edit wrapping"); guest.wait_for("Ctrl+S: save")
            lines = guest.screen().splitlines()
            assert lines[0] == "a" * (COLS - 1) + "b" and lines[1].startswith("cd")
            guest.key("ctrl", "end")
            assert guest.terminal_state()["cursor"] == COLS + 2
            guest.key("z"); guest.key("ctrl", "s"); guest.wait_for("Saved.")
            guest.key("esc"); guest.wait_for("IDE0>")
            guest.type("hexedit binary"); guest.wait_for("w: save")
            guest.key("pgdn")
            hex_page = f"{16 * (ROWS - 3):08X}:"
            assert hex_page in guest.wait_for(hex_page), "Hex Page Down did not move a full page"
            guest.key("pgup"); guest.wait_for("00000000:")
            guest.key("esc"); guest.wait_for("IDE0>")
            guest.type("view editing")
            guest.wait_for("PgUp/PgDn: scroll")
            guest.key("esc"); guest.wait_for("IDE0>")
            print("PASS guest: text editor, binary editor, viewer", flush=True)
            assert "nested-ok" in guest.run("nested")
            assert "nested.txt" not in guest.run("list")
            guest.run("drawline 10 10 20 10 14")
            state = guest.terminal_state()
            address = state["pixels"] + (10 * state["pitch"] + 10) * 4
            assert struct.unpack("<I", guest.memory(address, 4))[0] == 0xffff55
            guest.run("cls")
            assert struct.unpack("<I", guest.memory(address, 4))[0] == 0x0000aa
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
        subprocess.run([writer, "--extract", str(image), "multiline", str(output)], check=True)
        expected = multiline.read_bytes().replace(b"line01\n", b"line01z\n", 1)
        assert output.read_bytes() == expected, "Text editor navigation saved at the wrong position"
        subprocess.run([writer, "--extract", str(image), "wrapping", str(output)], check=True)
        assert output.read_bytes() == wrapping.read_bytes() + b"z", "Editing after a soft wrap saved at the wrong position"
        # An unsupported VGA adapter must keep its original text geometry and footer.
        guest = Guest(image, scratch, vga="cirrus")
        try:
            guest.wait_for("IDE0>"); assert not guest.terminal_address()
            guest.type("edit multiline"); guest.wait_for("Ctrl+S: save")
            assert "Ctrl+S: save" in guest.screen().splitlines()[24]
            guest.key("pgdn"); assert guest.wait_for("line24").startswith("line24")
            guest.key("esc"); guest.wait_for("IDE0>")
            guest.type("hexedit binary"); guest.wait_for("w: save")
            guest.key("pgdn"); guest.wait_for("00000160:")
            guest.key("esc"); guest.wait_for("IDE0>")
            print("PASS guest: legacy text console geometry and editors", flush=True)
        finally:
            guest.close()
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
