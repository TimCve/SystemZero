#!/usr/bin/env python3
"""Build boot sectors and update a populated image without losing guest files."""
import fcntl
from contextlib import ExitStack
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from disk_image import create, geometry, install_boot

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)

def call(*args):
    subprocess.run([str(a) for a in args], check=True)

def extract(image, name, scratch):
    output = Path(scratch) / "extracted"
    result = subprocess.run([str(ROOT / "ESFS_raw_write"), "--extract", str(image), name, str(output)], capture_output=True)
    if not result.returncode:
        return output.read_bytes()
    if b"file not found" in result.stderr:
        return None
    raise ValueError(f"cannot read {name}: {result.stderr.decode().strip()}")

def write_file(image, name, data, scratch):
    source = Path(scratch) / "payload"
    source.write_bytes(data)
    call(ROOT / "ESFS_raw_write", "--replace", image, name, source)

def remove_tree(image, name):
    """Remove a guest directory and its contents from the staged image."""
    result = subprocess.run([str(ROOT / "ESFS_raw_write"), "--list", str(image), name], capture_output=True)
    if result.returncode:
        if b"file not found" in result.stderr:
            return
        if b"parent is not a directory" in result.stderr:
            call(ROOT / "ESFS_raw_write", "--delete", image, name)
            return
        raise ValueError(f"cannot list {name}: {os.fsdecode(result.stderr).strip()}")
    for line in result.stdout.splitlines():
        kind, _, child = line.split(b'\t', 2)
        path = f"{name}/{os.fsdecode(child)}"
        if kind == b"dir":
            remove_tree(image, path)
        elif kind == b"file":
            call(ROOT / "ESFS_raw_write", "--delete", image, path)
        else:
            raise ValueError(f"invalid directory entry in {name}")
    call(ROOT / "ESFS_raw_write", "--delete", image, name)

def preserve(image, name, data, scratch):
    """Retain conflicting user edits instead of overwriting them during migration."""
    call(ROOT / "ESFS_raw_write", "--mkdir", image, "home/legacy")
    target = f"home/legacy/{name}"
    suffix = 0
    while (previous := extract(image, target, scratch)) is not None and previous != data:
        suffix += 1
        target = f"home/legacy/{name}.{suffix}"
    write_file(image, target, data, scratch)

def install_environment(image, scratch):
    defaults = dict(re.findall(r'^SZ_ENV\("([A-Z_]+)", "([^"\n]*)"\)$', Path("shared/environment.def").read_text(), re.M))
    limits = {name: int(value) for name, value in re.findall(r'^#define (SZ_ENV_\w+) (\d+)u$', Path("shared/environment.h").read_text(), re.M)}
    saved = extract(image, "sys/config/environment", scratch)
    legacy = extract(image, "systemzero.cfg", scratch)
    if legacy is not None:
        # Keep the original record for recovery; the runtime also understands it.
        previous = extract(image, "sys/config/legacy.cfg", scratch)
        if previous is not None and previous != legacy:
            preserve(image, "systemzero.cfg", legacy, scratch)
        else:
            write_file(image, "sys/config/legacy.cfg", legacy, scratch)
        call(ROOT / "ESFS_raw_write", "--delete", image, "systemzero.cfg")
    else:
        legacy = extract(image, "sys/config/legacy.cfg", scratch)
    if legacy is not None and len(legacy) == 16:
        magic, offset, position, color, secondary, reserved, check = struct.unpack("<IIBBBBI", legacy)
        expected = magic ^ offset ^ position ^ (color << 8) ^ (secondary << 16) ^ 0x18B52C4D
        valid = magic in (0x535A4331, 0x535A4332) and offset < 86400 and position < 2 and not reserved and check == expected and color & 15 != color >> 4
        if magic == 0x535A4331:
            valid &= secondary == 0
            secondary = 15 if color >> 4 == 14 else 14
        valid &= secondary < 16 and secondary != color >> 4
        if valid:
            defaults.update(CLOCK_OFFSET=str(offset), STATUS_POSITION="bottom" if position else "top", COLOR_FOREGROUND=str(color & 15), COLOR_BACKGROUND=str(color >> 4), COLOR_SECONDARY=str(secondary))
    values = defaults.copy()
    if saved is not None:
        try:
            text = saved.decode("ascii")
            if not text.endswith("\n") or len(saved) > limits["SZ_ENV_FILE"]:
                raise ValueError("invalid environment record")
            for line in text.splitlines():
                name, value = line.split("=", 1)
                if not re.fullmatch(r"[A-Z_][A-Z0-9_]*", name) or len(name) >= limits["SZ_ENV_NAME"] or len(value) >= limits["SZ_ENV_VALUE"] or any(not 32 <= ord(c) <= 126 for c in value):
                    raise ValueError("invalid environment value")
                if name not in ("VERSION", "PWD", "SYSTEM_VOLUME"):
                    values[name] = value
            if len(values) > limits["SZ_ENV_MAX"] or values["STATUS_POSITION"] not in ("top", "bottom") or not re.fullmatch(r"(?:sys:|[0-3](?:p(?:[1-9]|1[0-6]))?:)?/[^;:]*", values["HOME"]):
                raise ValueError("invalid environment settings")
            for key in ("COLOR_FOREGROUND", "COLOR_BACKGROUND", "COLOR_SECONDARY", "CLOCK_OFFSET"):
                if not values[key].isascii() or not values[key].isdecimal() or int(values[key]) > (86399 if key == "CLOCK_OFFSET" else 15):
                    raise ValueError("invalid numeric setting")
            if int(values["COLOR_BACKGROUND"]) in (int(values["COLOR_FOREGROUND"]), int(values["COLOR_SECONDARY"])):
                raise ValueError("invisible text color")
        except (UnicodeError, ValueError):
            preserve(image, "environment.invalid", saved, scratch)
            values = defaults.copy()
    # Replace the old generated search path, while retaining deliberately customized lists.
    if re.fullmatch(r"([0-3](?:p(?:[1-9]|1[0-6]))?):/bin;\1:/;\.", values["PATH"]):
        values["PATH"] = "sys:/bin"
    for key, suffix in (("INCLUDE", "/sys/include"), ("LIBRARY_PATH", "/sys/lib")):
        if re.fullmatch(r"0(?:p(?:[1-9]|1[0-6]))?:" + suffix, values[key]):
            values[key] = "sys:" + suffix
    for key in ("PATH", "INCLUDE", "LIBRARY_PATH", "HOME"):
        values[key] = ";".join("sys:" + p[2:] if p.startswith("0:") else p for p in values[key].split(';'))
    paths = ["sys:/bin" if p.rstrip('/') == "0:/bin" else p for p in values["PATH"].split(';')]
    if not any(p.rstrip('/') == "sys:/bin" for p in paths):
        paths.append("sys:/bin")
    values["PATH"] = ";".join(p for p in paths if p)
    if len(values["PATH"]) >= limits["SZ_ENV_VALUE"]:
        raise ValueError("PATH has no room for the system /bin directory")
    write_file(image, "sys/config/environment", "".join(f"{key}={value}\n" for key, value in values.items()).encode(), scratch)

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
    fresh = sys.argv[1] == "--fresh"
    if fresh and len(sys.argv) != 3:
        raise ValueError("usage: build_image.py --fresh IMAGE")
    image = Path(sys.argv[2] if fresh else sys.argv[1]).resolve()
    boot = Path("build/bootloader/boot_sect.bin").read_bytes()
    loader = Path("build/bootloader/loader.bin").read_bytes()
    kernel = Path("build/kernel/kernel.elf").read_bytes()
    if len(boot) != 512 or len(loader) > 54 * 512 or not 0 < len(kernel) <= (200 - 64) * 512:
        raise ValueError("boot binaries exceed their reserved disk regions")
    # Stage on the destination filesystem so the final replacement is atomic.
    with ExitStack() as locks, tempfile.TemporaryDirectory(prefix=".systemzero-image-", dir=image.parent) as scratch:
        staged = Path(scratch) / "disk.bin"
        existing = None
        if image.exists():
            # Respect the byte-range locks also used by QEMU.
            existing = locks.enter_context(image.open("r+b"))
            fcntl.lockf(existing, fcntl.LOCK_EX | fcntl.LOCK_NB)
            if not fresh:
                with staged.open("wb") as output:
                    shutil.copyfileobj(existing, output)
                call(ROOT / "ESFS_raw_write", "--check", staged)
        if fresh or existing is None:
            with staged.open("wb") as output:
                output.truncate(100 * 1024 * 1024)
            call(ROOT / "ESFS_raw_write", "--format", staged)
        info = geometry(ROOT / "ESFS_raw_write", staged)
        migrating = info['table'] == 0 and existing is not None and not fresh
        if info['table'] == 0:
            raw = Path(scratch) / 'system.bin'
            staged.rename(raw)
            create(staged, raw, boot)
            info = geometry(ROOT / "ESFS_raw_write", staged)
        install_boot(staged, info, boot, loader, kernel, ROOT / 'build/uefi/esp.bin')
        # Retired bundled commands are removed while unrelated guest files survive.
        call(ROOT / "ESFS_raw_write", "--delete", staged, "chain")
        remove_tree(staged, "sys/kernel")
        directories = {"bin", "docs", "home", "home/examples", "sys", "sys/config", "sys/include", "sys/include/systemzero", "sys/lib"}
        sdk_files = [(f"sys/include/{p.relative_to('build/sdk/include')}", p) for p in Path("build/sdk/include").rglob("*.h")]
        sdk_files += [(f"sys/lib/{p.name}", p) for p in Path("build/sdk/lib").glob("*") if p.suffix in (".o", ".a")]
        for name, _ in sdk_files:
            parent = Path(name).parent
            while str(parent) != ".":
                directories.add(str(parent)); parent = parent.parent
        for directory in sorted(directories, key=lambda name: (name.count('/'), name)):
            call(ROOT / "ESFS_raw_write", "--mkdir", staged, directory)
        call(ROOT / "ESFS_raw_write", "--delete", staged, "bin/chain")
        # Match the build's source list; stale ELF artifacts must not resurrect commands.
        for source in sorted(Path("external_programs").glob("*.c")):
            program = source.with_suffix(".elf")
            old = extract(staged, program.stem, scratch)
            if old is not None:
                previous = extract(staged, f"bin/{program.stem}", scratch)
                if old != previous and old != program.read_bytes():
                    preserve(staged, program.stem, old, scratch)
                call(ROOT / "ESFS_raw_write", "--delete", staged, program.stem)
            call(ROOT / "ESFS_raw_write", "--replace", staged, f"bin/{program.stem}", program)
        for name, source in sdk_files:
            call(ROOT / "ESFS_raw_write", "--replace", staged, name, source)
        for name, source in (("LICENSE", "LICENSE"), ("manual.txt", "manual.txt"), ("language.txt", "docs/language.md"), ("storage.txt", "docs/storage.md"), ("install.txt", "docs/install.md")):
            old = extract(staged, name, scratch)
            if old is not None:
                if old != Path(source).read_bytes():
                    preserve(staged, name, old, scratch)
                call(ROOT / "ESFS_raw_write", "--delete", staged, name)
            call(ROOT / "ESFS_raw_write", "--replace", staged, f"docs/{name}", source)
        for source in sorted(Path("examples").glob("*.sz")):
            name = f"home/examples/{source.name}"
            old, current = extract(staged, source.name, scratch), extract(staged, name, scratch)
            if old is not None:
                if current is None:
                    write_file(staged, name, old, scratch)
                elif current != old:
                    preserve(staged, source.name, old, scratch)
                call(ROOT / "ESFS_raw_write", "--delete", staged, source.name)
            elif current is None:
                call(ROOT / "ESFS_raw_write", "--replace", staged, name, source)
        if extract(staged,"sys/config/completion",scratch) is None:
            write_file(staged,"sys/config/completion",Path("config/completion").read_bytes(),scratch)
        install_environment(staged, scratch)
        call(ROOT / "ESFS_raw_write", "--check", staged)
        if image.exists():
            backup = image.with_name(image.name + ".bak")
            existing.seek(0)
            with backup.open("wb") as output:
                shutil.copyfileobj(existing, output)
            print(f"Previous image saved to {backup.name}")
            if migrating:
                original = image.with_name(image.stem + '.pre-gpt' + image.suffix + '.bak')
                if not original.exists():
                    existing.seek(0)
                    with original.open('wb') as output:
                        shutil.copyfileobj(existing, output)
                    print(f"Original whole-disk image retained as {original.name}")
        os.replace(staged, image)
        if image == ROOT / 'os-image.bin':
            Path('build/uefi/boot.img').unlink(missing_ok=True)
        state = "fresh filesystem; current bundled programs installed" if fresh else "bundled programs installed; existing guest files preserved"
        print(f"Ready: {image.name} ({state})")

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Image build failed: {error}", file=sys.stderr)
        sys.exit(1)
