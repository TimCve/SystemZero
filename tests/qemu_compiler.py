#!/usr/bin/env python3
"""Compile and run native programs inside SystemZero on disposable disks."""
from pathlib import Path
import shutil
import subprocess
import struct
import tempfile

from qemu_smoke import Guest, ROOT


def main():
    subprocess.run(["make", "build/tests/api_compat.elf"], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    with tempfile.TemporaryDirectory(prefix="systemzero-compiler-qemu-") as scratch:
        image = Path(scratch) / "disk.bin"
        shutil.copyfile(ROOT / "os-image.bin", image)
        extra = Path(scratch) / "extra.bin"
        with extra.open("wb") as stream: stream.truncate(8 * 1024 * 1024)
        bad = Path(scratch) / "bad.sz"
        bad.write_text("int main() {\n  int x = ;\n}\n")
        writer = str(ROOT / "ESFS_raw_write")
        for name, source in (("compiler-test.sz", ROOT / "tests/compiler_language.sz"), ("bad.sz", bad),
                             ("input.sz", ROOT / "tests/compiler_input.sz"),
                             ("api-check", ROOT / "build/tests/api_compat.elf")):
            subprocess.run([writer, "--replace", str(image), name, str(source)], check=True)
        guest = Guest(image, scratch, extra)
        try:
            guest.wait_for("IDE0>")
            initial_heap = guest.heap_pointer()
            guest.run("cc", "1 -> exit status")
            guest.run("cc missing.sz output", "1 -> exit status")
            guest.run("cc hello.sz hello.sz", "1 -> exit status")
            guest.run("cc hello.sz hello")
            assert "compiled inside SystemZero" in guest.run("hello")
            assert "1 4 9 16 25" in guest.run("hello")
            guest.run("copy 0:hello 0:saved-hello")
            assert "bad.sz:2:11:" in guest.run("cc bad.sz hello", "1 -> exit status")
            assert "compiled inside SystemZero" in guest.run("hello"), "A compile error damaged the previous executable"
            assert "api-compat-ok" in guest.run("api-check")
            guest.run("cc files.sz files")
            assert "Created inside SystemZero." in guest.run("files")
            guest.run("cc compiler-test.sz ctest")
            screen = guest.run("ctest")
            assert "compiler-language-ok" in screen and "-2147483648" in screen and "FFFFFFFF" in screen, screen
            guest.run("ctest")  # Repeated allocations/nested execution must reclaim memory.
            guest.run("cc draw.sz draw")
            guest.run("draw")
            state = guest.terminal_state()
            for x, y, expected in ((20, 80, 0xffff55), (400, 60, 0xff5555)):
                address = state["pixels"] + (y * state["pitch"] + x) * 4
                assert struct.unpack("<I", guest.memory(address, 4))[0] == expected, "Native graphics API drew the wrong pixel"
            guest.screenshot(ROOT / "build/compiler-graphics.png")
            guest.run("cls")
            guest.run("cc input.sz input")
            guest.type("input"); guest.wait_for("Input:")
            guest.type("xyz"); guest.wait_for("Key:"); guest.key("q")
            screen = guest.wait_for("input-ok")
            assert "Text: xyz" in screen and "113" in screen
            guest.wait_for("IDE0>"); guest.run("cls")
            guest.type("input"); guest.wait_for("Input:"); guest.key("esc")
            guest.wait_for("input-cancelled"); guest.wait_for("IDE0>")
            # Write source with the guest editor, compile it, and execute the result.
            guest.run("touch live.sz")
            guest.type("edit live.sz"); guest.wait_for("Ctrl+S: save")
            guest.type('int main() {\nprint("guest-written");\nreturn 0;\n}\n', submit=False)
            guest.key("ctrl", "s"); guest.wait_for("Saved.")
            guest.key("esc"); guest.wait_for("IDE0>")
            guest.run("cc live.sz live")
            assert "guest-written" in guest.run("live")
            # cc is found on IDE0 while source/output and the resulting program live on IDE1.
            guest.run("drive 1"); guest.run("format")
            guest.run("copy 0:hello.sz 1:hello.sz")
            guest.run("cc hello.sz other")
            assert "compiled inside SystemZero" in guest.run("other")
            guest.run("drive 0")
            assert guest.heap_pointer() == initial_heap, "Compiler or native API execution leaked temporary heap memory"
        except BaseException:
            print(guest.screen())
            print(guest.command("human-monitor-command", {"command-line": "info registers"}))
            raise
        finally:
            guest.close()
        restored = Path(scratch) / "message"
        subprocess.run([writer, "--extract", str(image), "message.txt", str(restored)], check=True)
        assert restored.read_bytes() == b"Created inside SystemZero.\n"
        subprocess.run([writer, "--extract", str(image), "hello", str(restored)], check=True)
        compiled = restored.read_bytes()
        subprocess.run([writer, "--extract", str(image), "saved-hello", str(restored)], check=True)
        assert restored.read_bytes() == compiled, "A compile error changed the previous executable bytes"
        subprocess.run([writer, "--extract", str(image), "language.txt", str(restored)], check=True)
        assert restored.read_bytes() == (ROOT / "docs/language.md").read_bytes()
        subprocess.run([writer, "--check", str(image)], check=True)
        subprocess.run([writer, "--check", str(extra)], check=True)
        print("In-system compiler and native kernel API checks passed; original disk was untouched.")


if __name__ == "__main__":
    main()
