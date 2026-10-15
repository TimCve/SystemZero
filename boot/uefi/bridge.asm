; Position-independent trampoline, copied below 4 GiB before ExitBootServices.
; SysV arguments: EDI=kernel entry, ESI=env32, EDX=stack top.
bits 64
    cli
    cld
    mov ebp, edi
    mov ebx, esi
    mov edi, edx
    lea rax, [rel gdt]
    mov [rel descriptor + 2], rax
    lgdt [rel descriptor]
    push qword 8
    lea rax, [rel protected32]
    push rax
    retfq
bits 32
protected32:
    mov ax, 16
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov eax, cr0
    and eax, 0x7fffffff
    mov cr0, eax
    mov ecx, 0xc0000080
    rdmsr
    and eax, 0xfffffeff
    wrmsr
    mov eax, cr4
    and eax, 0xffffffdf
    mov cr4, eax
    mov esp, edi
    sub esp, 12
    push ebx
    call ebp
halt:
    cli
    hlt
    jmp halt
align 8
gdt:
    dq 0
    dq 0x00cf9a000000ffff
    dq 0x00cf92000000ffff
descriptor:
    dw 23
    dq 0
