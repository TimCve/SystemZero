all: prep os-image.bin

run: all
	qemu-system-x86_64 -drive format=raw,file=os-image.bin

# assemble boot sector
build/bootloader/boot_sect.bin: boot/boot_sect.asm
	nasm boot/boot_sect.asm -f bin -o build/bootloader/boot_sect.bin

# compile second stage bootloader
build/bootloader/loader_2.o: boot/*.c kernel/drivers/*/*.c
	gcc -g -m32 -ffreestanding -c boot/*.c kernel/drivers/*/*.c -nostartfiles -nostdlib
	mv loader.o build/bootloader/loader_2.o
	mv *.o build/drivers/

# link object files into kernel loader
build/bootloader/loader.elf: build/bootloader/loader_2.o
	ld -m elf_i386 -nostdlib -T ld/loader.ld build/bootloader/loader_2.o build/drivers/*.o -o build/bootloader/loader.elf
	rm build/bootloader/loader_2.o

# assemble first stage bootloader
build/bootloader/loader.o: boot/loader.asm
	nasm boot/loader.asm -f elf -o build/bootloader/loader.o

# link first and second stage bootloaders
build/bootloader/loader_full.elf: build/bootloader/loader.o build/bootloader/loader.elf
	ld -m elf_i386 -nostdlib -T ld/loader.ld build/bootloader/loader.o build/bootloader/loader.elf -o build/bootloader/loader_full.elf

# make bootloader binary file
build/bootloader/loader.bin: build/bootloader/loader_full.elf
	objcopy build/bootloader/loader_full.elf -O binary build/bootloader/loader.bin
	./scripts/pad_loader.sh
	# clean up unnecessary files
	rm build/bootloader/*.o build/bootloader/*.elf build/drivers/*.o

# compile kernel & write to 10MB raw drive image
build/kernel/hdd.bin: kernel/drivers/*/*.c kernel/*.c
	gcc -g -m32 -ffreestanding kernel/drivers/*/*.c kernel/*.c -nostartfiles -nostdlib -o build/kernel/kernel.o -T ld/kernel.ld
	./scripts/write_kernel_to_drive.sh

os-image.bin: build/bootloader/boot_sect.bin build/bootloader/loader.bin build/kernel/hdd.bin
	cat build/bootloader/boot_sect.bin build/bootloader/loader.bin build/kernel/hdd.bin > os-image.bin

prep:
	mkdir -p build/bootloader
	mkdir -p build/drivers
	mkdir -p build/kernel 

clean:
	rm -rf build/
	rm *.bin







