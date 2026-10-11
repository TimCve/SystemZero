[bits 32]
section .text.startup
global start
extern loadkernel
extern __bss_start
extern __bss_end
start:
    cld
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
    push dword [esp+4]
    call loadkernel
    cli
.halt:
    hlt
    jmp .halt
section .note.GNU-stack noalloc noexec nowrite progbits
