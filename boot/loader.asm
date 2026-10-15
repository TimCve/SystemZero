[bits 32]
section .text.startup
global start
extern loadkernel
extern __bss_start
extern __bss_end
start:
    cld
    mov ebx, [esp+4]
    mov edx, [esp+8]
    ; The loader's BSS can cover the boot sector. Keep descriptors in the
    ; loader image before clearing BSS, so later interrupts can reload CS.
    lgdt [loader_gdt_pointer]
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
    push edx
    push ebx
    call loadkernel
    cli
.halt:
    hlt
    jmp .halt
section .rodata
align 8
loader_gdt:
    dq 0
    dq 0x00cf9a000000ffff
    dq 0x00cf92000000ffff
loader_gdt_pointer:
    dw 23
    dd loader_gdt
section .note.GNU-stack noalloc noexec nowrite progbits
