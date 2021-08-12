CC_FLAGS = -Wl,--allow-multiple-definition -g -m32 -ffreestanding -nostartfiles -nostdlib -fno-stack-protector
LD_FLAGS = --allow-multiple-definition -m elf_i386 -nostdlib

CC := gcc ${CC_FLAGS}
LD := ./ld_patch/ld-new ${LD_FLAGS} 

BOOTLOADER_DRIVERS = kernel/drivers/disk/ata.c kernel/drivers/io/screen.c kernel/drivers/utils/mem.c kernel/drivers/utils/ports.c

raw: prep os-image.bin

convert_vmdk: os-image.vmdk	

all: prep os-image.bin

ESFS_raw_write: ESFS_raw_write.c
	gcc $^ -o $@

# run OS in QEMU
run: 
	qemu-system-i386 -drive format=raw,file=os-image.bin

# assemble boot sector
build/bootloader/boot_sect.bin: boot/boot_sect.asm
	nasm $^ -f bin -o $@

# compile second stage bootloader
build/bootloader/loader_2.o build/drivers/*.o: boot/*.c ${BOOTLOADER_DRIVERS}
	${CC} -c $^
	mv loader.o build/bootloader/loader_2.o
	mv *.o build/drivers/

# link object files into kernel loader
build/bootloader/loader.elf: build/bootloader/loader_2.o build/drivers/*.o
	${LD} -T ld/loader.ld $^ -o $@
	rm build/bootloader/loader_2.o

# assemble first stage bootloader
build/bootloader/loader.o: boot/loader.asm
	nasm $^ -f elf -o $@

# link first and second stage bootloaders
build/bootloader/loader_full.elf: build/bootloader/loader.o build/bootloader/loader.elf
	${LD} -T ld/loader.ld $^ -o $@

# make bootloader binary file
build/bootloader/loader.bin: build/bootloader/loader_full.elf
	objcopy $^ -O binary $@
	./scripts/pad_loader.sh
	# clean up unnecessary files
	# rm build/bootloader/*.o build/bootloader/*.elf build/drivers/*.o

build/kernel/interrupt.o: kernel/cpu/interrupt.asm
	nasm $^ -f elf -o $@

# compile kernel & write to 100MB raw drive image
build/kernel/hdd.bin: kernel/drivers/*/*.c kernel/cpu/*.c kernel/libc/*.c build/kernel/interrupt.o kernel/*.c
	${CC} $^ -o build/kernel/kernel.o -T ld/kernel.ld
	./scripts/write_kernel_to_drive.sh

# concat 3 boot stages into os-image file
os-image.bin: build/bootloader/boot_sect.bin build/bootloader/loader.bin build/kernel/hdd.bin
	cat $^ > $@

os-image.vmdk: 
	VBoxManage convertfromraw os-image.bin os-image.vmdk --format VMDK
	VBoxManage internalcommands sethduuid os-image.vmdk 6372c00a-a62e-4241-9a21-90fa4c22f019

# prepare directory structure for build process
prep:
	mkdir -p build/bootloader
	mkdir -p build/drivers
	mkdir -p build/kernel

# clean up build files and os-image binary
clean:
	-rm -rf build/
	-rm *.bin *.vmdk
	-rm ESFS_raw_write
	-rm external_programs/*.elf
