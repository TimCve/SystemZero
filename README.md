# OSDev
A basic operating system project built entirely from scratch.

### Makefile rules

- `make all` - builds the operating system disk image (10MB) in raw binary mode only
- `make raw` - (same as make all) builds the operating system disk image (10MB) in raw binary mode only
- `make convert_vmdk` - converts the raw binary OS image into vmdk mode, leaving the raw binary image
- `make clean` - clean up build files and executables
- `make run` - runs the system in QEMU (no compilation/building is done)
- `make ESFS_raw_write` - builds the program required for writing files from host machine to ESFS on raw binary disk

### How to copy files from host machine to OS virtual hard disk

I have written a program that is able to copy the exact bytes of a file on your host machine to the virtual hard disk of the OS, to compile it view the "Makefile rules" section. The program is only able to write data to a raw binary virtual disk (os-image.bin). DO NOT TRY TO WRITE TO os-image.vmdk.

Usage:
- `make clean raw ESFS_raw_write`
- `ESFS_raw_write os-image.bin file_name_in_ESFS file_name_on_local_machine`
- `make convert_vmdk`

You will now be able to either boot the .bin image in QEMU or the .vmdk image in Virtualbox/VMware and it will contain the file(s) which you have copied onto the disk. Keep in mind that once a disk is converted to .vmdk mode, it can no longer be written to by ESFS_raw_write.

