# SystemZero

SystemZero is a 32-bit operating system with a graphical text console, ESFS
filesystem, command shell, threads, and a C-like compiler.

The kernel launches `/bin/shell` from the system filesystem at startup.

## Build and run

Install the host tools on Debian:

```sh
sudo apt update
sudo apt install build-essential binutils clang nasm python3 \
    qemu-system-x86 qemu-system-gui ovmf
make -j4 all
make run-uefi
```

The shell starts in `/home` at `DISK0p2/home>`. Try:

```text
list
list sys:/bin
view sys:/docs/manual.txt
cc examples/hello.sz hello
./hello
tree
```

All default QEMU profiles attach one `os-image.bin` disk:

| Partition | Format | Size    | Purpose                   |
| --------- | ------ | ------: | ------------------------- |
| `0p1:`    | FAT32  | 64 MiB  | EFI loader and kernel     |
| `0p2:`    | ESFS   | 100 MiB | Programs, files, settings |

The disk is 166 MiB including alignment and GPT metadata. `sys:` addresses
its ESFS system volume.

To build a UEFI installer ISO that can also be written directly to a USB drive:

```sh
sudo apt install genisoimage
make -j4 iso
```

The output is `SystemZero-install.iso`. It installs a fresh system onto a
selected IDE, SATA/AHCI, or NVMe destination, replacing its partitions and files.
The installer protects its source disk and verifies the written data.
Use a spare disk: physical hardware is untested. See the
[installation guide](docs/install.md) for USB preparation, requirements, and
VM testing. `make run-installer` uses a separate virtual test disk.

Up/Down recalls commands from the shell's last 64 entries. Recalled commands
can be edited before Enter; Down past the newest entry restores unfinished
input. History lasts for the current shell session. Empty or whitespace-only
input opens another prompt.

Tab completes command names from `PATH` and file arguments for configured
commands. Repeated Tab cycles matches; Shift+Tab cycles back. Configure rules
in `sys:/sys/config/completion`, beside the environment file. For example,
`mycommand=paths` enables file/directory arguments for a new command.
See [completion rules](docs/storage.md#shell-completion).

### QEMU profiles

| Command              | Firmware | Storage   | Keyboard/mouse |
| -------------------- | -------- | --------- | -------------- |
| `make run-uefi`      | UEFI     | NVMe      | USB            |
| `make run-uefi-ahci` | UEFI     | SATA/AHCI | USB            |
| `make run-uefi-ide`  | UEFI     | IDE       | PS/2           |
| `make run`           | BIOS     | IDE       | PS/2           |

UEFI profiles use Q35, except the IDE profile, and allocate 256 MiB of RAM.
BIOS allocates 64 MiB. UEFI requires OVMF; set `OVMF_DIR` to its directory if
it is outside `/usr/share/OVMF`.

For a UEFI display without VGA support:

```sh
python3 scripts/run_uefi.py --display bochs
```

## Console and settings

The graphical terminal uses an 8x8 bitmap font in a 96x48 text grid. The status
bar shows the version, current command with arguments, and a 24-hour clock.
Text mode provides an 80x24 grid with a reserved status row.

| Command    | Purpose                                      |
| ---------- | -------------------------------------------- |
| `edit`     | Edit text; Ctrl+S saves, Escape exits        |
| `zedit`    | Edit source with completion; F5 builds/runs   |
| `hexedit`  | Edit bytes; W or Ctrl+S saves, Escape exits  |
| `view`     | Read a file with scrolling                   |
| `tree`     | Display files and directories recursively     |
| `settings` | Set clock, status position, and three colors |
| `env`      | Edit persistent environment variables        |
| `diskman`  | Partition disks and format ESFS volumes      |

Editors and the viewer repaint changed cells. Closing a fullscreen utility
restores the saved console. The prompt and boot logo use `COLOR_SECONDARY`.

`zedit app.sz` opens or creates a source file. It highlights syntax and shows
source line numbers, completion signatures, and origins. Wrapped continuations
leave the number column blank. Ctrl+Space opens completion; Tab accepts.
F5 saves, compiles, and runs in a separate console. Press any key after the
program finishes to return to the same editor view. See the
[IDE keys and linking options](docs/language.md#zedit).

Left-click places the caret in the current command or editor. Drag to select
text; Ctrl+C copies and Ctrl+V pastes. The guest clipboard holds up to 64 KiB
and is shared by the console, `edit`, `zedit`, `hexedit`, and `view`. The viewer
supports copying. Hex selections and paste operate on raw bytes.

The wheel scrolls three lines at a time. Editors keep the insertion point when
scrolling. The terminal retains 2,048 output rows; Shift+Page Up/Page Down also
scrolls this history, and typing returns to the live prompt. Up/Down continues
to recall commands. Pasting into a command turns line breaks and control
characters into spaces and stops at the command's input limit.

## Files and builds

| Target              | Result                                       |
| ------------------- | -------------------------------------------- |
| `make all`          | Update boot files, programs, and references  |
| `make programs`     | Compile bundled commands                     |
| `make clean`        | Remove build artifacts; keep disk images     |
| `make fresh-image`  | Replace the disk with a populated filesystem |
| `make run_gtk`      | Launch the BIOS profile with GTK             |
| `make run_sdl`      | Launch the BIOS profile with SDL             |
| `make convert_vmdk` | Create a VirtualBox image via `VBoxManage`   |
| `make iso`          | Build the UEFI DVD/USB installer ISO         |
| `make run-installer` | Install onto a separate virtual test disk   |

`make all` preserves user files, saved settings, and edited source examples.
Image updates are staged and checked; `os-image.bin.bak` holds the disk from
before the update. Stop QEMU before rebuilding or importing files.

`make fresh-image` removes user files and settings, installs the bundled
programs and examples, and saves the replaced image as `os-image.bin.bak`.

Host file operations select the system ESFS volume automatically:

```sh
./ESFS_raw_write --replace os-image.bin home/example.sz example.sz
./ESFS_raw_write --extract os-image.bin home/example.sz exported.sz
./ESFS_raw_write --list os-image.bin home
./ESFS_raw_write --check os-image.bin
./ESFS_raw_write --geometry os-image.bin
```

Use `--volume NUMBER` to select another partition. See
[storage and environment](docs/storage.md) for paths, limits, and disk tools.

## Compile programs

Inside SystemZero:

```text
cc examples/draw.sz draw
./draw
cc examples/sdk.sz sdk io.o
./sdk
```

Built-ins such as `print` and `draw_line` call the kernel directly. SDK functions
such as `sz_print` use headers and linked wrapper objects. `cc -c` creates an
object; `ld` links objects and archives. The compiler implements a C-like subset
with scalar types, pointers, arrays, functions, globals, and header includes.

Read [the language and API reference](docs/language.md) for syntax and complete
service tables. The guest copies are `sys:/docs/language.txt` and
`sys:/docs/storage.txt`.

For host-built C programs, place a source file in `external_programs/`:

```sh
./compile_program hello.c hello.elf
./ESFS_raw_write --replace os-image.bin bin/hello external_programs/hello.elf
```

The C entry is `int main(env_vars_t *env, char *args)`. Host-built programs use
headers and compiled objects from the project source tree and build directory.

## Tests

| Command                | Coverage                                             |
| ---------------------- | ---------------------------------------------------- |
| `make test`            | Host checks with AddressSanitizer and UBSan          |
| `make test-qemu`       | BIOS shell, compiler, editors, and threads           |
| `make test-uefi`       | UEFI IDE, AHCI, and NVMe profiles                    |
| `make test-installer`  | DVD/USB installation, firmware entry, and cold boot |
| `make test-volumes`    | Partitions, environment, libraries, reboot           |
| `make test-zedit`      | External shell and IDE on BIOS and UEFI              |
| `make test-completion` | Blank prompts, path completion, and IDE line numbers |
| `make test-shell`      | History and file trees on BIOS, UEFI, text           |
| `make test-mouse`      | PS/2 and USB input, selection, clipboard, scrolling  |

QEMU tests use disposable copies of the disk. Filesystem timing is available
with `python3 tests/qemu_performance.py`.

## Hardware support

The drivers support 512-byte-sector IDE, AHCI, and NVMe disks, PS/2 keyboards
and mice (including wheel packets), and USB HID boot keyboards and relative
mice on xHCI root ports. USB keyboard and mouse can run together. UEFI graphics use
a 32-bit RGB/BGR framebuffer. Firmware boot services end before the kernel runs.

Physical hardware is untested. USB hubs, hotplug, kernel USB storage, Secure Boot,
SMP, and process isolation are unsupported. UEFI requires PIC/PIT/RTC hardware
and a framebuffer addressable below 4 GiB. ESFS has no journal or guaranteed
power-loss recovery.

See [boot and hardware details](docs/boot.md).
