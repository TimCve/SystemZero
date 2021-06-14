C_SOURCES = $(wildcard kernel/*.c kernel/drivers/*/*.c)
HEADERS = $(wildcard kernel/*.h kernel/drivers/*.h)

OBJ = ${C_SOURCES:.c=.o}

all: os-image.img

run: all
	qemu-system-x86_64 -drive format=raw,file=os-image.img

os-image.img: os-image.bin
	dd if=/dev/zero of=os-image.img ibs=1k count=1440
	dd if=os-image.bin of=os-image.img conv=notrunc

os-image.bin: boot/bootloader.bin kernel.bin
	cat $^ > os-image.bin

kernel.bin: kernel_entry.o ${OBJ}
	ld -o $@ -m elf_i386 -nostdlib -T linker.ld $^ --oformat binary

%.o : %.c ${HEADERS}
	gcc -fno-pie -m32 -ffreestanding -c $< -o $@

kernel_entry.o : boot/kernel_entry.asm
	nasm $^ -f elf -o $@

%.bin : %.asm
	nasm $< -f bin -I '../../16bit/' -o $@

clean:
	rm -rf *.bin *.o os-image.bin os-image.img
	rm -rf kernel/*.o boot/*.bin kernel/drivers/*.o







