# Boot and hardware

## Shared disk

BIOS and UEFI boot the same 166 MiB `os-image.bin`. It has a protective MBR,
primary/backup GPT tables, a 64 MiB FAT32 EFI partition, and a 100 MiB ESFS
system partition. Default QEMU profiles attach one storage device.

| Location                     | Contents                   |
| ---------------------------- | -------------------------- |
| MBR, first 446 bytes         | BIOS boot code             |
| GPT partition 1              | `EFI/BOOT/BOOTX64.EFI`     |
| ESFS relative sector 0       | Volume boot sector         |
| ESFS relative sectors 1-54   | Second-stage loader region |
| ESFS relative sector 63      | Kernel manifest            |
| ESFS relative sectors 64-199 | ELF32 kernel region        |
| ESFS relative sector 200     | Filesystem superblock      |

`build/uefi/esp.bin` is an intermediate FAT32 partition payload copied into
partition 1 during the image build. `scripts/disk_image.py` writes the layout
and updates boot files. Both boot paths identify the system volume through its
kernel manifest; `sys:` addresses that volume.

## BIOS sequence

1. Firmware executes the protective MBR's boot code.
2. The boot code loads the second stage using the recorded system-volume offset.
3. The loader reads the manifest and kernel from the ESFS reserved sectors.
4. It validates the ELF image, loads segments/BSS, and calls the 32-bit kernel.
5. The kernel initializes drivers, mounts ESFS, applies settings, and starts
   the executable `sys:/bin/shell` in `HOME`.

The loader requires at least 16 MiB of RAM. Its checked heap uses memory between
1 MiB and 16 MiB. BIOS graphics use standard VGA with an 800x600 framebuffer;
unsupported adapters use a text console.

## UEFI sequence

1. Firmware loads `EFI/BOOT/BOOTX64.EFI`, an AMD64 PE/COFF application.
2. The loader selects a Graphics Output Protocol mode and reserves kernel memory.
3. It loads its embedded ELF32 kernel and captures the firmware memory map.
4. It calls `ExitBootServices` and switches into 32-bit protected mode.
5. The kernel initializes its drivers, mounts ESFS, applies settings, and starts
   the executable `sys:/bin/shell` in `HOME`.

The shell owns command input, the prompt, and command dispatch. Each command
uses the kernel's ELF loader; returning releases its temporary memory and joins
its worker threads. `exit` returns to the caller. The kernel restarts the
startup shell if it exits; an unavailable `/bin/shell` produces a boot error.

The shell retains its last 64 commands in memory. Up/Down browses history;
recalled commands can be edited, and Down past the newest entry restores the
unfinished input. Empty lines and consecutive duplicates are omitted.

The loader prefers an 800x600 32-bit RGB/BGR mode. The framebuffer must fit below
4 GiB. A 32 MiB LoaderCode arena below 3 GiB holds the transition code, font,
environment, memory map, stack, kernel, and heap. The loader retries stale
memory-map keys without allocating between `GetMemoryMap` and `ExitBootServices`.
Firmware services are unavailable after the handoff.

## Drivers

| Interface | Implementation                                     |
| --------- | -------------------------------------------------- |
| PCI       | Configuration mechanism 1                          |
| IDE       | ATA PIO                                            |
| AHCI      | Polled commands, one slot, DMA bounce buffer       |
| NVMe      | Admin/I/O queues, first active namespace           |
| PS/2      | Keyboard, mouse, IntelliMouse wheel packets        |
| xHCI      | Directly connected HID keyboards and relative mice |
| Graphics  | Standard VGA or UEFI GOP                           |
| Timing    | PIC/PIT interrupts and CMOS RTC                    |

Storage uses 512-byte sectors. AHCI and NVMe allocate persistent DMA buffers;
flushes preserve filesystem write ordering. AHCI enables FIS reception before
checking a connected drive's signature. High PCI BARs use PAE mappings in a
reserved aperture; controller registers are uncached.

The USB driver takes controller ownership, configures its rings, enumerates up to eight directly connected HID devices at boot, and keeps
keyboard and mouse transfer rings active together. Keyboard reports produce
key events with modifiers and repeat. Mouse report descriptors describe buttons,
relative X/Y motion, wheel fields, and report IDs. Input queues, DMA buffers,
clipboard, and terminal history stay allocated across commands. The desktop
worker polls input and paints the pointer while foreground programs run.

Mouse rendering saves and restores the pixels beneath the pointer. Logical
text batches keep the displayed canvas available for cursor movement. Direct
graphics writes and snapshots protect the pointer's backing pixels while they
change or capture the canvas. Polling collects selection changes before repainting
so sustained dragging does not rasterize a frame for every hardware packet.
Text rendering leaves interrupts enabled and defers overlapping presentations. Text
selection is an overlay; it does not change file contents or console cells.
Fullscreen utilities preserve the terminal's history and selection alongside
its text and graphics. Clipboard contents remain available after closing them.

## Limits and verification

`make iso` builds `SystemZero-install.iso`, an x64 UEFI installer for DVD or
raw USB media. It uses firmware block services and installs a fresh copy of the
shared disk onto selected IDE/AHCI/NVMe storage. The source is protected; writing
requires explicit disk confirmation. See [installation](install.md) for the
layout, supported hardware, and USB instructions. `make test-installer` checks
installation and subsequent boots without installer media.

Physical hardware is untested. Required framebuffer/kernel addresses must fit
the 32-bit address space. USB hubs, hotplug, storage, EHCI/UHCI, Secure Boot,
SMP, and process isolation are unsupported. PIC/PIT/RTC hardware is required.

`make test-qemu` checks BIOS graphics and text consoles. `make test-uefi` checks
IDE, AHCI, and NVMe, including USB input and a display without VGA.
`make test-mouse` checks PS/2 and USB pointers, command caret placement,
selection/copy/paste, editor and viewer scrolling, framebuffer restoration,
terminal scrollback while a foreground command runs, sustained drags, and
pointer visibility during pending text updates.
`make test-volumes` checks alternate GPT slots, formatting, file persistence,
environment settings, and library linking. QEMU tests use disposable images.
