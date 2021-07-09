# OSDev
A basic operating system project built entirely from scratch.

### Makefile rules

- `make all` - builds the operating system disk image (10MB) in raw binary and vmdk mode
- `make raw` - builds the operating system disk image (10MB) in raw binary mode only
- `make vmdk` - builds the operating system disk image (10MB) in vmdk mode only
- `make clean` - clean up build files
- `make run` - runs the system in QEMU (no compilation/building is done)

`make clean all run` - rebuilds and auto-runs OS in QEMU
