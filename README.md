# SystemZero
A basic operating system project built entirely from scratch.

Official website: https://z3r0flag.github.io/SysZero

### Dependencies for building from source

`bash, gcc, nasm, make, (optionally virtualbox to convert .bin drive to .vmdk)`

To build, simply run this command in the project root:
`./automake`

### Makefile rules

- `make all` - builds the operating system disk image (100MB) in raw binary mode only
- `make raw` - (same as make all) builds the operating system disk image (100MB) in raw binary mode only
- `make convert_vmdk` - converts the raw binary OS image into vmdk mode, leaving the raw binary image
- `make clean` - clean up build files and executables
- `make run` - runs the system in QEMU (no compilation/building is done)
- `make run_gtk` - same as above but uses gtk display with gl acceleration
- `make run_sdl` - same as above but uses sdl display with gl acceleration
- `make ESFS_raw_write` - builds the program required for writing files from host machine to ESFS on raw binary disk

### How to copy files from host machine to OS virtual hard disk

I have written a program that is able to copy the exact bytes of a file on your host machine to the virtual hard disk of the OS, to compile it view the "Makefile rules" section. The program is only able to write data to a raw binary virtual disk (os-image.bin). DO NOT TRY TO WRITE TO os-image.vmdk.

Usage:
- `make clean raw ESFS_raw_write`
- `./ESFS_raw_write os-image.bin file_name_in_ESFS file_name_on_local_machine`
- `make convert_vmdk`

You will now be able to either boot the .bin image in QEMU or the .vmdk image in Virtualbox/VMware (if you have those installed) and it will contain the file(s) which you have copied onto the disk. Keep in mind that once a disk is converted to .vmdk mode, it can no longer be written to by ESFS_raw_write.

### Writing and compiling an executable to run on the OS

The kernel shell is an interface for executing binaries stored as files on the virtual hard disk. (For a C program to be automatically built and written onto the virtual disk by the Automake script, put the .c source file in external_programs)

- Write the program in C, you will be able to include any kernel API that is available. View the header files inside the kernel directory to see the options.

- Compilation can be done through the simple `compile_program` script in the format `../compile_program <input_file.c> <output_file>` **it is important that you execute the script while being in the external_programs directory.** The script will compile your program together will all available kernel API.

- Writing your program to the OS virtual disk can be done in the same way as writing a standard text file, the procedure is described in the "How to copy files from host machine to OS virtual hard disk" section.

### Automake script

The automake script is easy to use, it will build the operating system as well as all .c files in external_programs/ and write the executable binaries to the operating system's virtual drive, an option will be provided to write any additional files to the disk before conversion to VMDK format.

### Multiple drives

There exist two scripts `create_create_virtual_disks` and `run_with_extra_disks`. (The latter is just a single long qemu command to virtualize SystemZero with 4 virtual IDE drives). The first script creates 3 virtual IDE drives (the first one is the boot drive). These can be "connected" to any of the 4 IDE busses and accessed within SystemZero.

`run_with_extra_disks gtk` & `run_with_extra_disks sdl` options are also available. See Makefile rules to see what those options do.
