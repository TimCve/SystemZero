import fcntl
import hashlib
import json
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
        cls.compile(cls.writer, "ESFS_raw_write.c", "shared/esfs.c", "shared/partition.c")

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

    def test_directories_partitions_and_environment(self):
        binary = self.directory / "volumes"
        self.compile(binary, "tests/volume_checks.c", "shared/partition.c", "shared/esfs.c", "shared/environment.c")
        self.run_checked(binary)

    def test_object_linker_and_header_preprocessor(self):
        binary = self.directory / "linker"
        self.compile(binary, "tests/linker_checks.c", "shared/compiler.c", "shared/linker.c", "shared/elf.c", "shared/preprocess.c")
        self.run_checked(binary, ROOT / "build/sdk/lib/system.a")

    def test_native_compiler_diagnostics_bounds_and_elf_output(self):
        binary = self.directory / "compiler"
        self.compile(binary, "tests/compiler_checks.c", "shared/compiler.c", "shared/elf.c")
        self.run_checked(binary, ROOT / "tests/compiler_language.sz")

    def test_ata_transfer_and_timeouts(self):
        binary = self.directory / "ata"
        self.compile(binary, "tests/ata_checks.c", "kernel/drivers/disk/ata.c", extra=("-DATA_POLL_LIMIT=1000",))
        self.run_checked(binary)

    def test_installer_validation_copy_verification_and_gpt_relocation(self):
        binary = self.directory / "installer"
        self.compile(binary, "tests/installer_checks.c", "boot/installer/disk.c", "shared/partition.c")
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

    def test_ide_highlighting_scopes_signatures_and_imports(self):
        binary = self.directory / "ide"
        self.compile(binary, "tests/ide_checks.c", "shared/ide.c")
        self.run_checked(binary)

    def test_mouse_poll_batching_and_cursor_during_text_updates(self):
        binary = self.directory / "input-poll"
        self.compile(binary, "tests/input_poll_checks.c",
                     extra=("-ffreestanding", "-fno-builtin", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-Dmalloc=sz_malloc", "-Wno-int-to-pointer-cast"))
        self.run_checked(binary)

    def test_mouse_reports_and_text_selection(self):
        binary = self.directory / "mouse"
        self.compile(binary, "tests/mouse_checks.c", "shared/hid_mouse.c", "shared/text.c")
        self.run_checked(binary)

    def test_configurable_path_completion_and_argument_quoting(self):
        binary = self.directory / "completion"
        self.compile(binary, "tests/completion_checks.c", "shared/completion.c")
        self.run_checked(binary)

    def test_shell_history_navigation_and_bounded_storage(self):
        binary = self.directory / "history"
        self.compile(binary, "tests/history_checks.c")
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
            shutil.copytree(ROOT, project, ignore=shutil.ignore_patterns(".git", "build", "*.bin", "*.bin.bak", "*.vmdk", "*.iso", "*.elf", "ESFS_raw_write", "__pycache__", "virtual_disks"))
            result = subprocess.run(["make", "-j4", "all"], cwd=project, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn("warning:", result.stderr)
            disk = project / "os-image.bin"
            listing = subprocess.check_output([str(project / "ESFS_raw_write"), "--list", str(disk)], text=True)
            self.assertEqual({line.split('\t')[2] for line in listing.splitlines()}, {"bin", "docs", "home", "sys"})
            listing = self.run_checked(project / "ESFS_raw_write", "--list", disk, "sys").stdout
            self.assertEqual({line.split('\t')[2] for line in listing.splitlines()}, {"config", "include", "lib"})
            info=json.loads(self.run_checked(project / "ESFS_raw_write", "--geometry", disk).stdout)
            self.assertEqual((info['partition'],info['sectors']),(2,204800))
            installed = self.directory / "installed-completion"
            self.run_checked(project / "ESFS_raw_write", "--extract", disk, "sys/config/completion", installed)
            self.assertEqual(installed.read_bytes(), (ROOT / "config/completion").read_bytes())
            installed.write_bytes(b"edit=paths\nmycommand=directories\n")
            self.run_checked(project / "ESFS_raw_write", "--replace", disk, "sys/config/completion", installed)
            # Start the update from an old whole-disk ESFS image and verify migration.
            with disk.open('rb') as current:
                current.seek(info['first']*512)
                payload=current.read(info['sectors']*512)
            disk.write_bytes(payload)
            source = Path(temp) / "payload"; source.write_bytes(b"guest\0payload" * 6000)
            self.run_checked(project / "ESFS_raw_write", "--replace", disk, "my-file", source)
            before = hashlib.sha256(disk.read_bytes()).digest()
            result = subprocess.run(["make", "-j4", "all"], cwd=project, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(hashlib.sha256(disk.read_bytes()).digest(), before)
            if shutil.which("clang"):
                result = subprocess.run(["make", "-j4", "uefi"], cwd=project, capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn("warning:", result.stderr)
                self.assertEqual(hashlib.sha256(disk.read_bytes()).digest(), before)
                self.assertEqual((project / "build/uefi/BOOTX64.EFI").read_bytes()[:2], b"MZ")
            # Upgrade an image containing the retired command, with an orphan ELF on the host.
            self.run_checked(project / "ESFS_raw_write", "--replace", disk, "chain", project / "external_programs/list.elf")
            shutil.copyfile(project / "external_programs/list.elf", project / "external_programs/chain.elf")
            # Recreate the previous layout, including a modified example and saved settings.
            self.run_checked(project / "ESFS_raw_write", "--replace", disk, "list", project / "external_programs/list.elf")
            edited = Path(temp) / "edited.sz"; edited.write_bytes(b'int main(){print("keep my edits");return 0;}\n')
            self.run_checked(project / "ESFS_raw_write", "--replace", disk, "hello.sz", edited)
            self.run_checked(project / "ESFS_raw_write", "--delete", disk, "home/examples/hello.sz")
            preferences = Path(temp) / "environment"
            self.run_checked(project / "ESFS_raw_write", "--extract", disk, "sys/config/environment", preferences)
            saved = preferences.read_bytes().replace(b'PATH=sys:/bin\n', b'PATH=0:/bin;0:/;.\n').replace(b'COLOR_SECONDARY=14\n', b'COLOR_SECONDARY=6\n')
            saved = saved.replace(b'INCLUDE=sys:/sys/include\n', b'INCLUDE=0:/sys/include\n').replace(b'LIBRARY_PATH=sys:/sys/lib\n', b'LIBRARY_PATH=0:/sys/lib\n') + b'NOTE=user preference\n'
            # Fill all 32 old slots: adding HOME must not discard valid preferences.
            saved = saved.replace(b'HOME=sys:/home\n', b'')
            saved += b''.join(f'CUSTOM{i}=keep-{i}\n'.encode() for i in range(23))
            self.assertEqual(len(saved.splitlines()), 32)
            preferences.write_bytes(saved)
            self.run_checked(project / "ESFS_raw_write", "--replace", disk, "sys/config/environment", preferences)
            old_config = Path(temp) / "config"
            magic, offset, position, color, secondary = 0x535a4332, 1234, 1, 31, 10
            old_config.write_bytes(struct.pack('<IIBBBBI', magic, offset, position, color, secondary, 0, magic ^ offset ^ position ^ (color << 8) ^ (secondary << 16) ^ 0x18b52c4d))
            self.run_checked(project / "ESFS_raw_write", "--replace", disk, "systemzero.cfg", old_config)
            # Remove the installed internal tree completely, while keeping SDK
            # libraries and similarly named user directories outside that tree.
            for directory in ("sys/kernel", "sys/kernel/include", "sys/kernel/include/kernel", "sys/kernel/lib", "sys/kernel/lib/kernel", "sys/kernel-notes"):
                self.run_checked(project / "ESFS_raw_write", "--mkdir", disk, directory)
            for name, payload in (("sys/kernel/include/kernel/kernel.h", project / "kernel/kernel.h"),
                                  ("sys/kernel/lib/kernel/extra lib.o", project / "build/obj/kernel/libc/strings.o"),
                                  ("sys/kernel-notes/notes", source), ("sys/lib/custom.o", source)):
                self.run_checked(project / "ESFS_raw_write", "--replace", disk, name, payload)
            before = hashlib.sha256(disk.read_bytes()).digest()
            os.utime(project / "kernel/env_vars.h", None)
            result = subprocess.run(["make", "-j4", "all"], cwd=project, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            output = Path(temp) / "restored"
            self.run_checked(project / "ESFS_raw_write", "--extract", disk, "my-file", output)
            self.assertEqual(source.read_bytes(), output.read_bytes())
            listing = self.run_checked(project / "ESFS_raw_write", "--list", disk, "sys").stdout
            self.assertNotIn("kernel", {line.split('\t')[2] for line in listing.splitlines()})
            for name in ("sys/kernel-notes/notes", "sys/lib/custom.o"):
                self.run_checked(project / "ESFS_raw_write", "--extract", disk, name, output)
                self.assertEqual(output.read_bytes(), source.read_bytes())

            result = subprocess.run([str(project / "ESFS_raw_write"), "--extract", str(disk), "chain", str(output)], capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b"file not found", result.stderr)
            self.assertEqual(hashlib.sha256((project / "os-image.bin.bak").read_bytes()).digest(), before)
            info=json.loads(self.run_checked(project / 'ESFS_raw_write', '--geometry', disk).stdout)
            self.assertEqual((info['table'],info['partition'],info['sectors']),(2,2,204800))
            self.assertEqual(disk.stat().st_size,166*1024*1024)
            self.assertEqual(hashlib.sha256((project / 'os-image.pre-gpt.bin.bak').read_bytes()).digest(),before)
            for name in ("list", "hello.sz", "systemzero.cfg"):
                result = subprocess.run([str(project / "ESFS_raw_write"), "--extract", str(disk), name, str(output)], capture_output=True)
                self.assertEqual(result.returncode, 1, name)
            self.run_checked(project / "ESFS_raw_write", "--extract", disk, "home/examples/hello.sz", output)
            self.assertEqual(output.read_bytes(), edited.read_bytes())
            self.run_checked(project / "ESFS_raw_write", "--extract", disk, "sys/config/environment", output)
            migrated = output.read_bytes()
            self.run_checked(project / "ESFS_raw_write", "--extract", disk, "sys/config/completion", output)
            self.assertEqual(output.read_bytes(), installed.read_bytes())
            for value in (b'PATH=sys:/bin\n', b'INCLUDE=sys:/sys/include\n', b'LIBRARY_PATH=sys:/sys/lib\n', b'HOME=sys:/home\n', b'COLOR_SECONDARY=6\n', b'NOTE=user preference\n'):
                self.assertIn(value, migrated)
            for i in range(23):
                self.assertIn(f'CUSTOM{i}=keep-{i}\n'.encode(), migrated)
            self.run_checked(project / "ESFS_raw_write", "--extract", disk, "sys/config/legacy.cfg", output)
            self.assertEqual(output.read_bytes(), old_config.read_bytes())
            # A customized search path survives, with the system commands kept accessible.
            preferences.write_bytes(migrated.replace(b'PATH=sys:/bin\n', b'PATH=1p1:/tools;.\n'))
            self.run_checked(project / "ESFS_raw_write", "--replace", disk, "sys/config/environment", preferences)
            result = subprocess.run(["make", "clean"], cwd=project, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(disk.exists())
            result = subprocess.run(["./automake"], cwd=project, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.run_checked(project / "ESFS_raw_write", "--extract", disk, "my-file", output)
            self.assertEqual(source.read_bytes(), output.read_bytes())
            self.run_checked(project / "ESFS_raw_write", "--extract", disk, "sys/config/environment", output)
            self.assertIn(b'PATH=1p1:/tools;.;sys:/bin\n', output.read_bytes())
            self.assertIn(b'NOTE=user preference\n', output.read_bytes())
            self.run_checked(project / "ESFS_raw_write", "--extract", disk, "home/examples/hello.sz", output)
            self.assertEqual(output.read_bytes(), edited.read_bytes())

            if shutil.which("genisoimage") and shutil.which("clang"):
                # Distribution media must not inherit guest files or preferences.
                before = hashlib.sha256(disk.read_bytes()).digest()
                result = subprocess.run(["make", "-j4", "iso"], cwd=project, capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn("warning:", result.stderr)
                self.assertEqual(hashlib.sha256(disk.read_bytes()).digest(), before)
                payload = project / "build/installer/system.img"
                for name in ("my-file", "sys/lib/custom.o", "sys/kernel-notes/notes"):
                    result = subprocess.run([str(project / "ESFS_raw_write"), "--extract", str(payload), name, str(output)], capture_output=True)
                    self.assertEqual(result.returncode, 1)
                    self.assertIn(b"file not found", result.stderr)
                self.run_checked(project / "ESFS_raw_write", "--extract", payload, "sys/config/environment", output)
                self.assertIn(b"PATH=sys:/bin\n", output.read_bytes())
                self.assertNotIn(b"NOTE=", output.read_bytes())
                self.run_checked(project / "ESFS_raw_write", "--extract", payload, "home/examples/hello.sz", output)
                self.assertEqual(output.read_bytes(), (ROOT / "examples/hello.sz").read_bytes())

            # Binary-only preferences become a complete text environment on upgrade.
            legacy_disk = Path(temp) / "legacy.bin"; shutil.copyfile(disk, legacy_disk)
            self.run_checked(project / "ESFS_raw_write", "--delete", legacy_disk, "sys/config/environment")
            self.run_checked("python3", project / "scripts/build_image.py", legacy_disk)
            self.run_checked(project / "ESFS_raw_write", "--extract", legacy_disk, "sys/config/environment", output)
            for value in (b'CLOCK_OFFSET=1234\n', b'STATUS_POSITION=bottom\n', b'COLOR_SECONDARY=10\n'):
                self.assertIn(value, output.read_bytes())

            # The explicit reset target must work independently of all's mount check,
            # retain a recovery copy, and remove every old guest file/preference.
            self.run_checked(project / "ESFS_raw_write", "--replace", disk, "obsolete", project / "external_programs/list.elf")
            self.run_checked(project / "ESFS_raw_write", "--replace", disk, "systemzero.cfg", source)
            before = hashlib.sha256(disk.read_bytes()).digest()
            with disk.open("r+b") as locked:
                fcntl.lockf(locked, fcntl.LOCK_EX | fcntl.LOCK_NB)
                result = subprocess.run(["make", "fresh-image"], cwd=project, capture_output=True, text=True, timeout=60)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(hashlib.sha256(disk.read_bytes()).digest(), before)
            with disk.open("r+b") as corrupted:
                info=json.loads(self.run_checked(project / 'ESFS_raw_write', '--geometry', disk).stdout)
                corrupted.seek((info['first']+200) * 512); corrupted.write(b"BAD!")
            before = hashlib.sha256(disk.read_bytes()).digest()
            result = subprocess.run(["make", "-j4", "fresh-image"], cwd=project, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("fresh filesystem", result.stdout)
            self.assertEqual(disk.stat().st_size, 166 * 1024 * 1024)
            self.assertEqual(hashlib.sha256((project / "os-image.bin.bak").read_bytes()).digest(), before)
            self.run_checked(project / "ESFS_raw_write", "--check", disk)
            for name in ("my-file", "obsolete", "systemzero.cfg", "chain"):
                result = subprocess.run([str(project / "ESFS_raw_write"), "--extract", str(disk), name, str(output)], capture_output=True)
                self.assertEqual(result.returncode, 1)
            bundles = [("bin/" + path.stem, path.with_suffix(".elf")) for path in (project / "external_programs").glob("*.c")]
            bundles += [("home/examples/" + path.name, path) for path in (project / "examples").glob("*.sz")]
            bundles += [("docs/LICENSE", project / "LICENSE"), ("docs/manual.txt", project / "manual.txt"), ("docs/language.txt", project / "docs/language.md"), ("docs/storage.txt", project / "docs/storage.md")]
            bundles += [("sys/include/" + str(path.relative_to(project / "build/sdk/include")), path) for path in (project / "build/sdk/include").rglob("*.h")]
            bundles += [("sys/lib/" + path.name, path) for path in (project / "build/sdk/lib").glob("*") if path.suffix in (".o", ".a")]
            listing = self.run_checked(project / "ESFS_raw_write", "--list", disk, "sys").stdout
            self.assertEqual({line.split('\t')[2] for line in listing.splitlines()}, {"config", "include", "lib"})
            for name, expected in bundles:
                self.run_checked(project / "ESFS_raw_write", "--extract", disk, name, output)
                self.assertEqual(output.read_bytes(), expected.read_bytes(), name)

if __name__ == "__main__": unittest.main()
