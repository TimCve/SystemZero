# SystemZero
A basic operating system project built entirely from scratch.

Official website: https://z3r0flag.github.io/SysZero

## IMPORTANT NOTICE FOR BUILDING FROM SOURCE
If your system's LD version is greater than 2.34 (check by running `ld --version`), the build process will fail because of a regression bug in LD. I have created a repository containing GNU binutils version 2.34 at https://github.com/z3r0flag/binutils-2_34.git 

To build binutils 2.34:
- `cd <binutils repository root dir>`
- `./configure`
- `make`

To use LD 2.34 in build process for SystemZero:
- `mkdir <SystemZero repository root dir>/ld_patch`
- `cp <binutils repository root dir>/ld/* <SystemZero repository root dir>/ld_patch/`
- Edit `Makefile` in SystemZero root and set `LD := ./ld_patch/ld-new ${LD_FLAGS}`

DONE! Run `./automake` and test it out.

### Dependencies for building

`virtualbox, gcc, nasm, make`
*ld version must be <= 2.34*

### Makefile rules

- `make all` - builds the operating system disk image (100MB) in raw binary mode only
- `make raw` - (same as make all) builds the operating system disk image (100MB) in raw binary mode only
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

### Writing and compiling an executable to run on the OS

The kernel shell is an interface for executing binaries stored as files on the virtual hard disk. (For a C program to be automatically built and written onto the virtual disk by the Automake script, put the .c source file in external_programs)

- Write the program in C, you will be able to include any kernel API that is available. View the header files inside the kernel directory to see the options.

- Compilation can be done through the simple `compile_program` script in the format `./compile_program <input_file.c> <output_file>` it is important that you execute the script while being in the directory that the script is in. The script will compile your program together will all available kernel API.

- Writing your program to the OS virtual disk can be done in the same way as writing a standard text file, the procedure is described in the "How to copy files from host machine to OS virtual hard disk" section.

### Automake script

The automake script is easy to use, it will build the operating system as well as all .c files in external_programs/ and write the executable binaries to the operating system's virtual drive, an option will be provided to write any additional files to the disk before conversion to VMDK format.

### Multiple drives

There exist two scripts `create_create_virtual_disks` and `run_with_extra_disks`. (The latter is just a single long qemu command to virtualize SystemZero with 4 virtual IDE drives). The first script creates 3 virtual IDE drives (the first one is the boot drive). These can be "connected" to any of the 4 IDE busses and accessed within SystemZero.

