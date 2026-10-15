#!/usr/bin/env python3
"""Time filesystem commands after Enter, on a disposable copy of an image."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from qemu_smoke import Guest, ROOT, COLS, ROWS


def run(guest, command):
    # Type without submitting; keyboard delivery time is outside the measurement.
    guest.type(command, submit=False)
    start = time.monotonic()
    guest.command("send-key", {"keys": [{"type": "qcode", "data": "ret"}], "hold-time": 20})
    while time.monotonic() - start < 30:
        screen = guest.screen()
        cursor = guest.terminal_state()["cursor"]
        row = cursor // COLS
        if row < ROWS and cursor % COLS == len("DISK0p2> ") and \
                screen.splitlines()[row].strip() in ("DISK0p2>", "DISK1>", "DISK2>", "DISK3>"):
            return time.monotonic() - start
        time.sleep(.01)
    raise AssertionError(f"Command {command!r} timed out:\n{screen}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "os-image.bin")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="systemzero-performance-") as scratch:
        image = Path(scratch) / "disk.bin"
        shutil.copyfile(args.image, image)
        payload = Path(scratch) / "payload"
        payload.write_bytes(bytes(range(256)) * 300)
        subprocess.run([str(ROOT / "ESFS_raw_write"), "--replace", str(image),
                        "perf-source", str(payload)], check=True)
        guest = Guest(image, scratch)
        results = {}
        try:
            guest.boot_root()
            for command in ("list", "list", "list", "touch perf-new", "del perf-new",
                            "copy sys:perf-source sys:perf-copy"):
                elapsed = run(guest, command)
                results.setdefault(command, []).append(elapsed)
                print(f"{command}: {elapsed:.3f} s", flush=True)
        finally:
            guest.close()
        extracted = Path(scratch) / "extracted"
        subprocess.run([str(ROOT / "ESFS_raw_write"), "--extract", str(image),
                        "perf-copy", str(extracted)], check=True)
        assert extracted.read_bytes() == payload.read_bytes()
    if args.json:
        args.json.write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
