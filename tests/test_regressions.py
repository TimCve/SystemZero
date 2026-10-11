import fcntl
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class RegressionChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix="systemzero-tests-")
        cls.directory = Path(cls.scratch.name)
        cls.writer = cls.directory / "writer"
        cls.compile(cls.writer, "ESFS_raw_write.c", "shared/esfs.c")

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    @classmethod
    def compile(cls, output, *sources, extra=()):
        args = ["gcc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
        subprocess.run(args + list(extra) + [str(ROOT / s) for s in sources] + ["-o", str(output)], check=True, capture_output=True)

    def run_checked(self, *args):
        result = subprocess.run([str(a) for a in args], capture_output=True, text=True,
                                env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1"}, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_filesystem_and_elf(self):
        binary = self.directory / "core"
        self.compile(binary, "tests/core_checks.c", "shared/esfs.c", "shared/elf.c")
        self.run_checked(binary)

    def test_native_compiler_diagnostics_bounds_and_elf_output(self):
        binary = self.directory / "compiler"
        self.compile(binary, "tests/compiler_checks.c", "shared/compiler.c", "shared/elf.c")
        self.run_checked(binary, ROOT / "tests/compiler_language.sz")

    def test_ata_transfer_and_timeouts(self):
        binary = self.directory / "ata"
        self.compile(binary, "tests/ata_checks.c", "kernel/drivers/disk/ata.c", extra=("-DATA_POLL_LIMIT=1000",))
        self.run_checked(binary)

    def test_filesystem_cache_batching_and_flush_boundaries(self):
        binary = self.directory / "cache"
        self.compile(binary, "tests/fs_cache_checks.c", "shared/esfs.c")
        self.run_checked(binary)

    def test_framebuffer_incremental_rendering_editor_navigation_and_keyboard(self):
        binary = self.directory / "display"
        self.compile(binary, "tests/display_checks.c", "shared/terminal.c", "shared/text.c",
                     "kernel/drivers/io/keyboard.c", extra=("-ffunction-sections", "-Wl,--gc-sections"))
        self.run_checked(binary)

    def test_allocator_bounds_alignment_cleanup(self):
        binary = self.directory / "memory"
        self.compile(binary, "tests/memory_checks.c", "kernel/drivers/utils/mem.c",
                     extra=("-fPIE", "-pie", "-Dmalloc=sz_malloc", "-Dmemcpy=sz_memcpy", "-Dmemset=sz_memset", "-Wno-int-to-pointer-cast"))
        self.run_checked(binary)

    def new_disk(self, name, sectors=8192):
        path = self.directory / name
        with path.open("wb") as f: f.truncate(sectors * 512)
        self.run_checked(self.writer, "--format", path)
        return path

    def test_host_binary_append_large_file_replace_and_extract(self):
        disk = self.new_disk("binary.bin")
        source = self.directory / "input"
        source.write_bytes(b"A\0B")
        self.run_checked(self.writer, disk, "binary", source)
        source.write_bytes(b"C")
        self.run_checked(self.writer, "--append", disk, "binary", source)
        output = self.directory / "output"
        self.run_checked(self.writer, "--extract", disk, "binary", output)
        self.assertEqual(output.read_bytes(), b"A\0BC")
        data = bytes(range(256)) * 310 + b"tail"
        source.write_bytes(data)
        self.run_checked(self.writer, "--replace", disk, "binary", source)
        self.run_checked(self.writer, "--extract", disk, "binary", output)
        self.assertEqual(output.read_bytes(), data)
        self.run_checked(self.writer, "--check", disk)
        before = disk.read_bytes()
        result = subprocess.run([str(self.writer), "--extract", str(disk), "binary", str(disk)], capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(disk.read_bytes(), before)

    def test_host_input_errors_do_not_modify_disk(self):
        disk = self.new_disk("errors.bin")
        original = disk.read_bytes()
        source = self.directory / "oversized"
        source.write_bytes(bytes(1024 * 1024 + 1))
        cases = [(disk, "missing", self.directory / "not-found"),
                 (disk, "large", source), (disk, "n" * 512, source),
                 (disk, "", source), (disk,), ("--bogus", disk),
                 ("--delete", disk), ("--delete", disk, "")]
        for args in cases:
            result = subprocess.run([str(self.writer), *(str(a) for a in args)], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 1, result.stderr.decode())
            self.assertNotIn(b"Sanitizer", result.stderr)
            self.assertEqual(disk.read_bytes(), original)

    def test_host_delete_preserves_other_files_and_is_repeatable(self):
        disk = self.new_disk("delete.bin")
        source = self.directory / "delete-input"
        payload = bytes(range(256)) * 300
        source.write_bytes(payload)
        self.run_checked(self.writer, "--replace", disk, "removed", source)
        self.run_checked(self.writer, "--replace", disk, "kept", source)
        self.run_checked(self.writer, "--delete", disk, "removed")
        self.run_checked(self.writer, "--check", disk)
        output = self.directory / "delete-output"
        self.run_checked(self.writer, "--extract", disk, "kept", output)
        self.assertEqual(output.read_bytes(), payload)
        result = subprocess.run([str(self.writer), "--extract", str(disk), "removed", str(output)], capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"file not found", result.stderr)
        before = disk.read_bytes()
        self.run_checked(self.writer, "--delete", disk, "removed")
        self.assertEqual(disk.read_bytes(), before)

    def test_host_disk_full_preserves_replaced_file(self):
        disk = self.new_disk("full.bin", 220)
        source = self.directory / "full-input"
        source.write_bytes(b"original")
        self.run_checked(self.writer, "--replace", disk, "kept", source)
        original = disk.read_bytes()
        source.write_bytes(bytes(20000))
        result = subprocess.run([str(self.writer), "--replace", str(disk), "kept", str(source)], capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(disk.read_bytes(), original)

    def test_host_refuses_locked_or_invalid_disk(self):
        disk = self.new_disk("locked.bin")
        source = self.directory / "locked-input"; source.write_bytes(b"data")
        with disk.open("r+b") as locked:
            fcntl.lockf(locked, fcntl.LOCK_EX | fcntl.LOCK_NB)
            result = subprocess.run([str(self.writer), str(disk), "test", str(source)], capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b"in use", result.stderr)
        disk.write_bytes(bytes(220 * 512 + 1))
        result = subprocess.run([str(self.writer), "--check", str(disk)], capture_output=True)
        self.assertEqual(result.returncode, 1)

    def test_incremental_parallel_build_preserves_guest_files(self):
        with tempfile.TemporaryDirectory(prefix="systemzero-build-") as temp:
            project = Path(temp) / "repo"
            shutil.copytree(ROOT, project, ignore=shutil.ignore_patterns(".git", "build", "*.bin", "*.bin.bak", "*.vmdk", "*.elf", "ESFS_raw_write", "__pycache__", "virtual_disks"))
            result = subprocess.run(["make", "-j4", "all"], cwd=project, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn("warning:", result.stderr)
            disk = project / "os-image.bin"
            source = Path(temp) / "payload"; source.write_bytes(b"guest\0payload" * 6000)
            self.run_checked(project / "ESFS_raw_write", "--replace", disk, "my-file", source)
            before = hashlib.sha256(disk.read_bytes()).digest()
            result = subprocess.run(["make", "-j4", "all"], cwd=project, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(hashlib.sha256(disk.read_bytes()).digest(), before)
            # Upgrade an image containing the retired command, with an orphan ELF on the host.
            self.run_checked(project / "ESFS_raw_write", "--replace", disk, "chain", project / "external_programs/list.elf")
            shutil.copyfile(project / "external_programs/list.elf", project / "external_programs/chain.elf")
            before = hashlib.sha256(disk.read_bytes()).digest()
            os.utime(project / "kernel/env_vars.h", None)
            result = subprocess.run(["make", "-j4", "all"], cwd=project, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            output = Path(temp) / "restored"
            self.run_checked(project / "ESFS_raw_write", "--extract", disk, "my-file", output)
            self.assertEqual(source.read_bytes(), output.read_bytes())
            result = subprocess.run([str(project / "ESFS_raw_write"), "--extract", str(disk), "chain", str(output)], capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b"file not found", result.stderr)
            self.assertEqual(hashlib.sha256((project / "os-image.bin.bak").read_bytes()).digest(), before)
            result = subprocess.run(["make", "clean"], cwd=project, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(disk.exists())
            result = subprocess.run(["./automake"], cwd=project, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.run_checked(project / "ESFS_raw_write", "--extract", disk, "my-file", output)
            self.assertEqual(source.read_bytes(), output.read_bytes())

if __name__ == "__main__": unittest.main()
