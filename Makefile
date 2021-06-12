C_SOURCES = $(wildcard kernel/*.c kernel/drivers/*.c)
HEADERS = $(wildcard kernel/*.h kernel/drivers/*.h)

OBJ = ${C_SOURCES:.c=.o}

all: os-image

run: all
	qemu-system-x86_64 -drive format=raw,file=os-image

os-image: boot/bootloader.bin kernel.bin
	cat $^ > os-image

kernel.bin: kernel_entry.o ${OBJ}
	ld -o $@  -m elf_i386 -Ttext 0x1000 $^ --oformat binary

%.o : %.c ${HEADERS}
	gcc -fno-pie -m32 -ffreestanding -c $< -o $@

kernel_entry.o : boot/kernel_entry.asm
	nasm $^ -f elf -o $@

%.bin : %.asm
	nasm $< -f bin -I '../../16bit/' -o $@

clean:
	rm -rf *.bin *.o os-image
	rm -rf kernel/*.o boot/*.bin kernel/drivers/*.o






