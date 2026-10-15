# Installation

## Build the media

Install the normal build tools from the README, plus `genisoimage`:

```sh
sudo apt install genisoimage
make -j4 iso
```

The output is `SystemZero-install.iso`, approximately 198 MiB. It boots as a
UEFI DVD in a VM or as a USB drive written directly from the same image.
The installer is a standalone x64 UEFI application. It uses firmware filesystem
and block services; it does not run the SystemZero kernel while installing.

The payload is a freshly populated system disk with default settings and the
current programs, examples, headers, and libraries. Building the installer
does not read or change `os-image.bin` or include its user files. The staged
payload is `build/installer/system.img`.

## Hardware requirements

| Component | Requirement |
| --------- | ----------- |
| Firmware | x64 UEFI; Secure Boot disabled |
| CPU | x86 with protected mode and PAE; one core is used |
| Memory | 256 MiB or more; 512 MiB recommended |
| Destination | Writable IDE, SATA/AHCI, or NVMe disk/namespace |
| Capacity | At least 166 MiB; fewer than 4,294,967,296 logical sectors |
| Sector size | 512 bytes logical; 512e is accepted, 4Kn is unsupported |
| Display | UEFI GOP with an RGB/BGR framebuffer below 4 GiB |
| Input after boot | PS/2 or a directly connected USB boot keyboard/mouse on xHCI |
| Timing | Legacy PIC/PIT interrupts and CMOS RTC |

SATA controllers must use AHCI rather than RAID/RST/VMD. NVMe uses the first
active namespace. USB storage is supported by firmware during installation;
the installed kernel has no USB-storage driver, so USB destination disks are
excluded. USB hubs, hotplug, APIC interrupt routing, and SMP are unsupported.

**Physical hardware is untested. Use a spare disk with backups. Successful
installation confirms that firmware can access the disk; it does not confirm
that the kernel supports every device in the computer.**

## Write a USB installer

Use a USB drive of at least 256 MiB. Writing the image replaces the USB drive's
partition layout and files. In GNOME Disks, select the USB drive, unmount its
partitions, and use **Restore Disk Image** with `SystemZero-install.iso`.
Use a raw/DD image-writing mode in other tools; copying the ISO as a file onto
a mounted USB filesystem does not create installation media.

To write it from Linux's command line, identify the USB drive first:

```sh
lsblk -o NAME,SIZE,MODEL,SERIAL,TRAN,MOUNTPOINTS
```

Unmount its mounted partitions. Replace the placeholder below with the USB
drive's actual `/dev/disk/by-id/usb-...` path. Select the whole device, without
a `-partN` suffix, and verify its model, serial, and capacity before writing:

```sh
sudo dd if=SystemZero-install.iso \
    of=/dev/disk/by-id/usb-REPLACE_WITH_YOUR_USB_DEVICE \
    bs=4M conv=fsync status=progress
```

## Install and boot

1. Boot the USB using the computer's **UEFI** boot-menu entry.
2. Use Left/Right to select the destination. Check its capacity and complete
   firmware device path. Enter opens the final review; Escape cancels.
3. Type the displayed `ERASE DISK N` phrase exactly to begin.
4. Wait for image validation, copying, flushing, and read-back verification.
5. Remove the USB and press Enter to restart. Select **SystemZero** in the
   firmware boot menu if necessary.

**Installation replaces every partition on the chosen destination. It does
not install beside another OS or preserve existing files.** Nothing is written
before confirmation and successful validation of the complete bundled image.
The installer excludes its source disk, partitions, read-only devices, devices
without identifiable paths, and unsupported target sizes/types.

The installed layout is:

| Partition | Format | Size | Contents |
| --------- | ------ | ---: | -------- |
| 1 | FAT32 ESP | 64 MiB | `EFI/BOOT/BOOTX64.EFI`, with the embedded kernel |
| 2 | ESFS | 100 MiB | Programs, SDK, examples, documents, and configuration |

Remaining capacity is unallocated and can hold additional volumes created
with `diskman`. The installer relocates the backup GPT to the destination's
actual end, updates both headers and checksums, and assigns fresh disk and
partition identifiers. It does not securely erase unallocated disk contents.

After verification, the installer creates an active UEFI **SystemZero** boot
entry and puts it first in `BootOrder`, preserving existing entries. If firmware
rejects NVRAM updates, the installer reports that separately. Use the firmware's
**Boot from File** function to select `\EFI\BOOT\BOOTX64.EFI` on partition 1.
No signed bootloader is provided; Secure Boot must remain disabled.

An interrupted or failed installation can leave the destination unusable.
Boot the installer again and reinstall; do not treat a partially written disk
as a successful installation.

## Virtual-machine checks

```sh
make run-installer
python3 scripts/run_installer.py --usb --storage ahci
make test-installer
```

Interactive runs use `build/installer/test-disk.img`, a 1 GiB virtual disk
created only if absent. They never select a host block device. The ISO and
test disk are separate; installing does not change the development system disk.

For VirtualBox, attach the ISO as an optical disc to an x64 UEFI VM with a
separate blank SATA/AHCI disk, one vCPU, 512 MiB RAM, GOP graphics, and PS/2
input. After installing, detach the ISO and boot the virtual hard disk.
VirtualBox has not been validated.

Automated QEMU/OVMF tests cover DVD-to-AHCI and USB-to-NVMe installation,
including a USB image padded to a larger stick capacity. They check cancellation,
source-media protection, GPT relocation, the saved firmware boot entry, cold
boots without installer media, compilation, and persistent settings. Host tests
exercise image corruption, capacity bounds, failed reads/writes/flushes,
verification failures, and optical/USB image metadata.

The media layout follows [UEFI media-access protocols](https://uefi.org/specs/UEFI/2.10/13_Protocols_Media_Access.html).
