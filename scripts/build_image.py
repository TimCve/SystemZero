#!/usr/bin/env python3
"""Build boot sectors and update a populated image without losing guest files."""
import fcntl
from contextlib import ExitStack
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)

def call(*args):
    subprocess.run([str(a) for a in args], check=True)

def main():
    if sys.argv[1] == "--layout":
        loader = Path(sys.argv[2]).read_bytes()
        sectors = (len(loader) + 511) // 512
        if not 1 <= sectors <= 54:
            raise ValueError("second-stage loader exceeds its BIOS load region")
        Path(sys.argv[3]).write_text(f"%define LOADER_SECTORS {sectors}\n")
        return
    if sys.argv[1] == "--clean":
        shutil.rmtree("build", ignore_errors=True)
        Path("ESFS_raw_write").unlink(missing_ok=True)
        for path in Path("external_programs").glob("*.elf"):
            path.unlink()
        return
    image = Path(sys.argv[1]).resolve()
    boot = Path("build/bootloader/boot_sect.bin").read_bytes()
    loader = Path("build/bootloader/loader.bin").read_bytes()
    kernel = Path("build/kernel/kernel.elf").read_bytes()
    if len(boot) != 512 or len(loader) > 54 * 512 or not 0 < len(kernel) <= (200 - 64) * 512:
        raise ValueError("boot binaries exceed their reserved disk regions")
    with ExitStack() as locks, tempfile.TemporaryDirectory(prefix="image-", dir="build") as scratch:
        staged = Path(scratch) / "disk.bin"
        if image.exists():
            # Respect the byte-range locks also used by QEMU.
            existing = locks.enter_context(image.open("r+b"))
            fcntl.lockf(existing, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with staged.open("wb") as output:
                shutil.copyfileobj(existing, output)
            call(ROOT / "ESFS_raw_write", "--check", staged)
        else:
            with staged.open("wb") as output:
                output.truncate(100 * 1024 * 1024)
            call(ROOT / "ESFS_raw_write", "--format", staged)
        with staged.open("r+b") as output:
            output.write(bytes(200 * 512))
            output.seek(0); output.write(boot)
            output.seek(512); output.write(loader)
            output.seek(63 * 512); output.write(struct.pack("<3I", 0x535A4B31, 64, len(kernel)))
            output.seek(64 * 512); output.write(kernel)
        # Retired bundled commands are removed while unrelated guest files survive.
        call(ROOT / "ESFS_raw_write", "--delete", staged, "chain")
        # Match the build's source list; stale ELF artifacts must not resurrect commands.
        for source in sorted(Path("external_programs").glob("*.c")):
            program = source.with_suffix(".elf")
            call(ROOT / "ESFS_raw_write", "--replace", staged, program.stem, program)
        for name in ("LICENSE", "manual.txt"):
            call(ROOT / "ESFS_raw_write", "--replace", staged, name, name)
        call(ROOT / "ESFS_raw_write", "--replace", staged, "language.txt", "docs/language.md")
        for source in sorted(Path("examples").glob("*.sz")):
            call(ROOT / "ESFS_raw_write", "--replace", staged, source.name, source)
        call(ROOT / "ESFS_raw_write", "--check", staged)
        if image.exists():
            backup = image.with_name(image.name + ".bak")
            existing.seek(0)
            with backup.open("wb") as output:
                shutil.copyfileobj(existing, output)
            print(f"Previous image saved to {backup.name}")
        os.replace(staged, image)
        print(f"Ready: {image.name} (bundled programs installed; existing guest files preserved)")

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Image build failed: {error}", file=sys.stderr)
        sys.exit(1)
