CC := gcc
HOSTCC := gcc
LD := ld
NASM := nasm
OBJCOPY := objcopy
PYTHON := python3
CFLAGS := -m32 -std=gnu11 -Os -Wall -Wextra -Wno-unused-parameter -ffreestanding -fno-builtin -fno-common -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -ffunction-sections -fdata-sections -mno-sse -mno-mmx -MMD -MP
PIEFLAGS := -fPIE
LDFLAGS := -m elf_i386 -pie --no-dynamic-linker -Bsymbolic --build-id=none --gc-sections
KERNEL_SOURCES := $(filter-out kernel/program_entry.c,$(shell find kernel -name '*.c')) shared/esfs.c shared/elf.c shared/terminal.c shared/text.c
KERNEL_OBJECTS := $(patsubst %.c,build/obj/%.o,$(KERNEL_SOURCES)) build/obj/kernel/cpu/interrupt.o
BOOT_SOURCES := boot/loader.c kernel/drivers/disk/ata.c kernel/drivers/io/screen.c kernel/drivers/io/video.c kernel/drivers/utils/mem.c kernel/drivers/utils/ports.c shared/elf.c shared/terminal.c
BOOT_OBJECTS := $(patsubst %.c,build/boot/%.o,$(BOOT_SOURCES)) build/boot/entry.o
PROGRAM_SOURCES := $(wildcard external_programs/*.c)
PROGRAMS := $(PROGRAM_SOURCES:.c=.elf)
DEPS := $(KERNEL_OBJECTS:.o=.d) $(BOOT_OBJECTS:.o=.d) $(PROGRAM_SOURCES:%.c=build/obj/%.d) build/obj/kernel/program_entry.d
.DEFAULT_GOAL := all
.PHONY: all raw programs run run_gtk run_sdl clean convert_vmdk test test-qemu
all raw: os-image.bin
programs: $(PROGRAMS)

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
	$(LD) $(LDFLAGS) -T ld/kernel.ld $(KERNEL_OBJECTS) -o $@

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

ESFS_raw_write: ESFS_raw_write.c shared/esfs.c shared/esfs.h
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror ESFS_raw_write.c shared/esfs.c -o $@

os-image.bin: build/bootloader/boot_sect.bin build/bootloader/loader.bin build/kernel/kernel.elf $(PROGRAMS) ESFS_raw_write LICENSE manual.txt scripts/build_image.py
	$(PYTHON) scripts/build_image.py $@

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

test: ESFS_raw_write
	$(PYTHON) -m unittest discover -s tests -v
test-qemu: all build/tests/large.elf
	$(PYTHON) tests/qemu_smoke.py

.SECONDARY:
-include $(DEPS) $(wildcard build/obj/tests/*.d)
