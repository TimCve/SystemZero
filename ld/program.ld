OUTPUT_FORMAT(elf32-i386)
ENTRY(program_start)
PHDRS {
    text PT_LOAD FILEHDR PHDRS FLAGS(5);
    data PT_LOAD FLAGS(6);
    dynamic PT_DYNAMIC FLAGS(6);
}
SECTIONS {
    . = SIZEOF_HEADERS;
    .text : { *(.text .text.*) } :text
    .rodata : { *(.rodata .rodata.*) } :text
    .dynsym : { *(.dynsym) } :text
    .dynstr : { *(.dynstr) } :text
    .hash : { *(.hash) } :text
    .gnu.hash : { *(.gnu.hash) } :text
    .rel.dyn : { *(.rel.*) } :text
    . = ALIGN(4096);
    .data : { *(.data .data.*) *(.got .got.*) } :data
    .dynamic : { *(.dynamic) } :data :dynamic
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) } :data
    /DISCARD/ : { *(.eh_frame* .comment .note*) }
}
