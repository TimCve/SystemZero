CC := gcc
HOSTCC := gcc
UEFICC := clang
LD := ld
NASM := nasm
OBJCOPY := objcopy
PYTHON := python3
CFLAGS := -m32 -std=gnu11 -Os -Wall -Wextra -Wno-unused-parameter -ffreestanding -fno-builtin -fno-common -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -ffunction-sections -fdata-sections -mno-sse -mno-mmx -MMD -MP
PIEFLAGS := -fPIE
CFLAGS += -DSYSTEMZERO_GUEST
LDFLAGS := -m elf_i386 -pie --no-dynamic-linker -Bsymbolic --build-id=none --gc-sections
KERNEL_SOURCES := $(filter-out kernel/program_entry.c,$(shell find kernel -name '*.c')) shared/esfs.c shared/elf.c shared/terminal.c shared/text.c shared/hid_mouse.c shared/partition.c shared/environment.c
KERNEL_OBJECTS := $(patsubst %.c,build/obj/%.o,$(KERNEL_SOURCES)) build/obj/kernel/cpu/interrupt.o
BOOT_SOURCES := boot/loader.c kernel/drivers/disk/ata.c kernel/drivers/io/screen.c kernel/drivers/io/video.c kernel/drivers/utils/mem.c kernel/drivers/utils/ports.c shared/elf.c shared/terminal.c
BOOT_OBJECTS := $(patsubst %.c,build/boot/%.o,$(BOOT_SOURCES)) build/boot/entry.o
PROGRAM_SOURCES := $(wildcard external_programs/*.c)
PROGRAMS := $(PROGRAM_SOURCES:.c=.elf)
EXAMPLES := $(wildcard examples/*.sz)
DEPS := $(KERNEL_OBJECTS:.o=.d) $(BOOT_OBJECTS:.o=.d) $(PROGRAM_SOURCES:%.c=build/obj/%.d) build/obj/kernel/program_entry.d build/obj/shared/compiler.d build/obj/shared/ide.d build/obj/shared/completion.d
.DEFAULT_GOAL := all
.PHONY: all raw programs run run_gtk run_sdl clean convert_vmdk test test-qemu
.PHONY: fresh-image
.PHONY: iso installer run-installer test-installer
.PHONY: test-volumes
.PHONY: uefi run-uefi run-uefi-ahci run-uefi-ide test-uefi
UEFIFLAGS := -m64 -std=gnu11 -Os -Wall -Wextra -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -mno-red-zone -mno-sse -mno-mmx -fno-asynchronous-unwind-tables -fno-ident
uefi: all

build/uefi/loader.o: boot/uefi/loader.c boot/uefi/efi.h boot/uefi/font8x8.h shared/boot.h shared/elf.h Makefile
	mkdir -p $(dir $@)
	$(UEFICC) --target=x86_64-w64-windows-gnu $(UEFIFLAGS) -c $< -o $@
build/uefi/elf.o: shared/elf.c shared/elf.h Makefile
	mkdir -p $(dir $@)
	$(UEFICC) --target=x86_64-w64-windows-gnu $(UEFIFLAGS) -c $< -o $@
build/uefi/bridge.bin: boot/uefi/bridge.asm
	mkdir -p $(dir $@)
	$(NASM) -f bin $< -o $@
build/uefi/kernel.o: build/kernel/kernel.elf
	mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O pe-x86-64 -B i386:x86-64 $< $@
build/uefi/bridge.o: build/uefi/bridge.bin
	$(OBJCOPY) -I binary -O pe-x86-64 -B i386:x86-64 $< $@
build/uefi/BOOTX64.EFI: build/uefi/loader.o build/uefi/elf.o build/uefi/kernel.o build/uefi/bridge.o
	$(LD) -m i386pep --subsystem 10 --entry efi_main --image-base 0x10000000 --enable-reloc-section $^ -o $@
build/uefi/esp.bin: build/uefi/BOOTX64.EFI scripts/build_uefi.py
	$(PYTHON) scripts/build_uefi.py $< $@

run-uefi: uefi
	$(PYTHON) scripts/run_uefi.py
run-uefi-ahci: uefi
	$(PYTHON) scripts/run_uefi.py --storage ahci
run-uefi-ide: uefi
	$(PYTHON) scripts/run_uefi.py --storage ide --ps2
test-uefi: uefi build/tests/console_state.elf
	$(PYTHON) tests/qemu_uefi.py
all raw: os-image.bin
programs: $(PROGRAMS)
build/sdk/stamp: scripts/build_sdk.py shared/sys_api.def
	$(PYTHON) scripts/build_sdk.py
external_programs/cc.elf: build/obj/shared/compiler.o build/obj/shared/linker.o build/obj/shared/preprocess.o
external_programs/ld.elf: build/obj/shared/linker.o
external_programs/zedit.elf: build/obj/shared/ide.o
external_programs/shell.elf: build/obj/shared/completion.o

build/obj/%.o: %.c Makefile
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(PIEFLAGS) -c $< -o $@

build/boot/%.o: %.c Makefile
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fno-pie -c $< -o $@

build/obj/kernel/cpu/interrupt.o: kernel/cpu/interrupt.asm Makefile
	mkdir -p $(dir $@)
	$(NASM) -f elf32 $< -o $@

build/boot/entry.o: boot/loader.asm Makefile
	mkdir -p $(dir $@)
	$(NASM) -f elf32 $< -o $@

build/kernel/kernel.elf: $(KERNEL_OBJECTS) ld/kernel.ld Makefile
	mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -T ld/kernel.ld $(KERNEL_OBJECTS) -o build/kernel/kernel.debug.elf
	$(OBJCOPY) --strip-all build/kernel/kernel.debug.elf $@

external_programs/%.elf: build/obj/external_programs/%.o build/obj/kernel/program_entry.o $(KERNEL_OBJECTS) ld/program.ld Makefile
	$(LD) $(LDFLAGS) -T ld/program.ld $(filter %.o,$^) -o $@

build/tests/%.elf: build/obj/tests/%.o build/obj/kernel/program_entry.o $(KERNEL_OBJECTS) ld/program.ld Makefile
	mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -T ld/program.ld $(filter %.o,$^) -o $@

build/bootloader/loader.elf: $(BOOT_OBJECTS) ld/loader.ld Makefile
	mkdir -p $(dir $@)
	$(LD) -m elf_i386 --build-id=none -T ld/loader.ld $(BOOT_OBJECTS) -o $@

build/bootloader/loader.bin: build/bootloader/loader.elf
	$(OBJCOPY) -O binary $< $@

build/bootloader/layout.inc: build/bootloader/loader.bin scripts/build_image.py
	$(PYTHON) scripts/build_image.py --layout $< $@

build/bootloader/boot_sect.bin: boot/boot_sect.asm $(wildcard boot/include/*.asm) build/bootloader/layout.inc
	$(NASM) -f bin $< -o $@

ESFS_raw_write: ESFS_raw_write.c shared/esfs.c shared/esfs.h shared/partition.c shared/partition.h
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror ESFS_raw_write.c shared/esfs.c shared/partition.c -o $@

IMAGE_INPUTS := build/bootloader/boot_sect.bin build/bootloader/loader.bin build/kernel/kernel.elf $(PROGRAMS) $(EXAMPLES) ESFS_raw_write LICENSE manual.txt docs/language.md docs/storage.md docs/install.md shared/environment.def config/completion scripts/build_image.py scripts/disk_image.py build/uefi/esp.bin build/sdk/stamp
.NOTINTERMEDIATE: $(IMAGE_INPUTS)
os-image.bin: $(IMAGE_INPUTS)
	$(PYTHON) scripts/build_image.py $@

# Explicit reset: stage a fresh filesystem and install only the current bundles.
fresh-image: $(IMAGE_INPUTS)
	$(PYTHON) scripts/build_image.py --fresh os-image.bin

# Installation always ships a fresh system, never the developer's guest files.
build/installer/system.img: $(IMAGE_INPUTS)
	mkdir -p $(dir $@)
	$(PYTHON) scripts/build_image.py --fresh $@
build/installer/payload.h: build/installer/system.img scripts/build_installer.py
	$(PYTHON) scripts/build_installer.py --manifest $< $@
build/installer/main.o: boot/installer/main.c boot/installer/protocols.h boot/installer/disk.h boot/uefi/efi.h build/installer/payload.h Makefile
	$(UEFICC) --target=x86_64-w64-windows-gnu $(UEFIFLAGS) -Ibuild/installer -c $< -o $@
build/installer/disk.o: boot/installer/disk.c boot/installer/disk.h Makefile
	mkdir -p $(dir $@)
	$(UEFICC) --target=x86_64-w64-windows-gnu $(UEFIFLAGS) -c $< -o $@
build/installer/BOOTX64.EFI: build/installer/main.o build/installer/disk.o
	$(LD) -m i386pep --subsystem 10 --entry efi_main --image-base 0x10000000 --enable-reloc-section $^ -o $@
SystemZero-install.iso: build/installer/BOOTX64.EFI build/installer/system.img scripts/build_installer.py scripts/build_uefi.py scripts/disk_image.py
	$(PYTHON) scripts/build_installer.py build/installer/BOOTX64.EFI build/installer/system.img $@
iso installer: SystemZero-install.iso
run-installer: iso
	$(PYTHON) scripts/run_installer.py
test-installer: iso
	$(PYTHON) tests/qemu_installer.py

os-image.vmdk: os-image.bin
	VBoxManage convertfromraw $< $@.tmp --format VMDK
	mv $@.tmp $@
convert_vmdk: os-image.vmdk

run: os-image.bin
	qemu-system-i386 -m 64M -vga std -drive format=raw,file=os-image.bin
run_gtk: os-image.bin
	qemu-system-i386 -m 64M -vga std -display gtk -drive format=raw,file=os-image.bin
run_sdl: os-image.bin
	qemu-system-i386 -m 64M -vga std -display sdl -drive format=raw,file=os-image.bin

# The disk image contains user files and survives cleaning build artifacts.
clean:
	$(PYTHON) scripts/build_image.py --clean

test: ESFS_raw_write build/sdk/stamp
	$(PYTHON) -m unittest discover -s tests -v
test-qemu: all build/tests/large.elf build/tests/nested.elf build/tests/console_state.elf
	$(PYTHON) tests/qemu_smoke.py
	$(PYTHON) tests/qemu_compiler.py
	$(PYTHON) tests/qemu_threads.py
test-volumes: uefi
	$(PYTHON) tests/qemu_layout.py
	$(PYTHON) tests/qemu_layout.py --uefi
	$(PYTHON) tests/qemu_volumes.py
	$(PYTHON) tests/qemu_volumes.py --uefi
	$(PYTHON) tests/qemu_partition_boot.py
	$(PYTHON) tests/qemu_environment.py
	$(PYTHON) tests/qemu_environment.py --uefi

.PHONY: test-zedit
test-zedit: all
	$(PYTHON) tests/qemu_zedit.py
	$(PYTHON) tests/qemu_zedit.py --uefi
	$(PYTHON) tests/qemu_zedit.py --text

.PHONY: test-shell
test-shell: all
	$(PYTHON) tests/qemu_shell.py
	$(PYTHON) tests/qemu_shell.py --uefi
	$(PYTHON) tests/qemu_shell.py --text

.PHONY: test-mouse
test-mouse: all
	$(PYTHON) tests/qemu_mouse.py
	$(PYTHON) tests/qemu_mouse.py --uefi
	$(PYTHON) tests/qemu_mouse.py --text
	$(PYTHON) tests/qemu_mouse_drag.py
	$(PYTHON) tests/qemu_mouse_drag.py --uefi
	$(PYTHON) tests/qemu_mouse_drag.py --text

.PHONY: test-completion
test-completion: all
	$(PYTHON) tests/qemu_completion.py
	$(PYTHON) tests/qemu_completion.py --uefi
	$(PYTHON) tests/qemu_completion.py --text

.SECONDARY:
-include $(DEPS) $(wildcard build/obj/tests/*.d)
