# SystemZero

A small 32-bit operating system with an ESFS filesystem and a command shell.

## Build and run in QEMU

On Debian, install the build tools and QEMU:

```sh
sudo apt update
sudo apt install build-essential binutils nasm python3 qemu-system-x86 qemu-system-gui
make -j4 all
make run
```

The build installs all bundled commands, `LICENSE`, and `manual.txt`. At the
`IDE0>` prompt, type `list`. `./automake` runs the same build and optionally
imports more files when used interactively.
When VirtualBox is installed, `./automake` also updates the VMDK image.

The boot path uses BIOS and IDE disks. The supplied QEMU commands provide
64 MiB of RAM; the loader requires at least 16 MiB. The allocator uses a checked
region from 1 MiB to 16 MiB. UEFI boot and physical hardware have not been tested.

The QEMU targets select standard VGA and use an 800×600, 32-bit framebuffer.
Text uses the compact BIOS 8×8 bitmap font with 12-pixel line spacing in a
centred 96×48 terminal. Small margins and space between lines keep the denser
layout readable; text editing and viewing show 47 content lines per page.
The character buffer, colours, scrolling, and software cursor are shared across
commands. Systems without a supported standard VGA adapter retain the text-mode
console. The driver follows QEMU's [standard VGA interface](https://www.qemu.org/docs/master/specs/standard-vga.html).

`edit`, `hexedit`, and `view` prepare each screen update in memory and repaint
only changed cells. Typing and scrolling do not clear the display first. The
text editor follows visual lines, expands tabs to four-column stops, keeps the
cursor visible, and preserves its column when moving across shorter lines.
Page Up/Down moves a full page. The hex editor shows 16 bytes per row, fixed-width
offsets, and an ASCII column; Escape cancels its byte-entry prompt.

## Keep files between builds

`make all` updates boot code and bundled files while preserving other ESFS files.
Retired bundled commands, including `chain`, are removed during the update.
The update is staged and checked before replacing `os-image.bin`; the previous
image is saved as `os-image.bin.bak`. Stop QEMU before building or importing files.
An image with an invalid filesystem is rejected rather than formatted.

`make clean` removes compiled artifacts and keeps disk images. To create a
fresh image, move the existing image aside and run `make all` again.

Additional targets:

- `make programs`: compile the bundled commands without updating the image.
- `make run_gtk` / `make run_sdl`: run QEMU with the selected display backend.
- `make test`: run host regression checks with AddressSanitizer and UBSan.
- `make test-qemu`: boot a disposable image and exercise the guest commands.
- `python3 tests/qemu_performance.py`: time filesystem commands on a disposable image.
- `make convert_vmdk`: convert the raw image for VirtualBox (requires `VBoxManage`).

QEMU checks also verify incremental redraw counts and save display previews to
`build/graphics-shell.png`, `build/graphics-editor.png`, and
`build/graphics-hex-editor.png`.

## Copy files from the host

```sh
make ESFS_raw_write
./ESFS_raw_write --replace os-image.bin guest-name path/to/source
./ESFS_raw_write --append os-image.bin guest-name path/to/more-data
./ESFS_raw_write --extract os-image.bin guest-name path/to/output
./ESFS_raw_write --delete os-image.bin guest-name
./ESFS_raw_write --check os-image.bin
```

Without an option, the writer appends, preserving the original CLI behavior.
Deleting an absent name succeeds, so cleanup can be repeated safely.
Names contain 1–511 bytes; each file can contain up to 916,992 payload bytes.
Files are binary-safe, including embedded zero bytes. Both host and guest use
`shared/esfs.c`. File sizes and read offsets refer to payload bytes, excluding
the filename. Guest `file_read` takes sector counts and offsets and pads the
last sector with zeroes.

New disks must be formatted explicitly:

```sh
truncate -s 100M new-disk.bin
./ESFS_raw_write --format new-disk.bin
```

`--format` erases filesystem metadata and files while preserving the first 200
sectors reserved for boot code. The writer accepts sector-aligned raw disks
smaller than 4 GiB and rejects missing inputs, oversized files, corrupt metadata,
I/O failures, and images locked by QEMU. Do not pass a VMDK to the raw writer.

Legacy images made by the original host writer are checked and mounted with
their overlapping final metadata sector reserved for data. The first write
migrates the superblock to the corrected format. Existing damaged metadata is
reported; the migration cannot repair previously corrupted file contents.

Replacement writes keep the old file until all replacement data has been
written and the new inode is published. This requires room for both versions.
ESFS has no journal or power-loss recovery guarantee.

The guest validates each drive on its first mount, then shares its allocation
map and metadata cache across commands. Metadata and contiguous file reads use
multiple-sector transfers. File changes update the shared cache immediately;
writes are flushed before publishing new file metadata and after publication.
Deleting a file flushes its removal before its blocks can be reused. Formatting
invalidates the drive's cache and performs a fresh validation.

## Compile guest programs

Inside SystemZero, use the bundled compiler and source examples:

    cc hello.sz hello
    hello

Write your own source with `touch` and `edit`, then run `cc SOURCE OUTPUT`.
The compiler emits native ELF32 programs that call a versioned kernel API for
printing, files, memory, keyboard input, and graphics. Compilation errors report
line/column locations and preserve existing output. Source and output use the
selected drive; commands are searched there before falling back to the system
drive. `hello.sz`, `files.sz`, and `draw.sz` are installed with the build.
The same language reference is installed as `language.txt` for `view language.txt`.

See [the language and kernel API reference](docs/language.md) for supported
syntax, examples, signatures, and limits. This is a C-like subset with functions,
arrays, pointers, conditionals, and loops; includes, full C compatibility, and
separate library/object linking are not implemented.

For host-built C programs, use the existing GCC workflow.

Put a C source file in `external_programs/` and use:

```sh
./compile_program hello.c hello.elf
./ESFS_raw_write --replace os-image.bin hello external_programs/hello.elf
```

Guest programs have `int main(env_vars_t *env, char *args)` and can include
kernel API headers. The build links freestanding ELF32 position-independent
executables without a Linux dynamic interpreter. The loader validates load
segments, clears BSS, applies supported relative relocations, and restores the
saved heap position when a command returns. Recompile custom executables after
kernel API changes; commands now share filesystem and terminal state through `env_vars_t`.
Programs using raw ATA writes directly must call `ata_flush()` for durability.
Use `screen_begin_update()` / `screen_end_update()` around a frame, and
`screen_put_cell()` / `screen_put_text()` for screen coordinates. The existing
print and cursor functions remain available; cursor offsets retain their old
two-bytes-per-cell convention. Write through these APIs rather than VGA text memory.

Header dependencies are tracked automatically. Object files have distinct
paths, parallel builds are supported, and duplicate symbol definitions fail
linking. The older dependency-inference and loader-padding scripts are no longer
part of the build.

## Multiple drives

`./create_virtual_disks` creates three additional raw disks and preserves
existing disks. `./run_with_extra_disks` attaches them as IDE1, IDE2, and IDE3.
Use `drives`, `drive 1`, and `list` in the guest; `manual.txt` describes the
bundled commands. The guest `format` command formats the selected disk.
