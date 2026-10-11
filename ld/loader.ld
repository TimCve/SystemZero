OUTPUT_FORMAT(elf32-i386)
ENTRY(start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x1000;
    .text : { KEEP(*(.text.startup)) *(.text .text.*) } :text
    .rodata : { *(.rodata .rodata.*) } :text
    .data : { *(.data .data.*) } :data
    .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.* COMMON) __bss_end = .; } :data
    /DISCARD/ : { *(.eh_frame* .comment .note*) }
    ASSERT(__bss_end < 0x80000, "Bootloader overlaps protected-mode stack")
}
