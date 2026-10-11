[bits 16]
[org 0x7c00]
%include "build/bootloader/layout.inc"
xor ax, ax
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x9000
cld
mov [BOOT_DRIVE], dl
sti
; Require at least 16 MiB for the checked 1..16 MiB heap.
mov ah, 0x88
int 0x15
jc failure
cmp ax, 15360
jb failure
mov si, packet
mov dl, [BOOT_DRIVE]
mov ah, 0x42
int 0x13
jc failure
; Preserve the BIOS 8x8 font before entering protected mode.
push ds
mov ax, 0x1130
mov bh, 3
int 0x10
mov si, bp
mov ax, es
mov ds, ax
mov ax, 0x8000
mov es, ax
xor di, di
mov cx, 2048
cld
rep movsb
pop ds
xor ax, ax
mov es, ax
; Enable A20 before using memory above one MiB.
in al, 0x92
or al, 2
and al, 0xfe
out 0x92, al
call switch_to_pm
jmp $
failure:
mov si, error
.next:
lodsb
test al, al
jz .stop
mov ah, 0x0e
int 0x10
jmp .next
.stop:
cli
hlt
jmp .stop
%include "boot/include/gdt.asm"
%include "boot/include/switch_to_pm.asm"
[bits 32]
BEGIN_PM:
movzx eax, byte [BOOT_DRIVE]
push eax
call 0x1000
jmp $
[bits 16]
align 4
packet: db 16, 0
dw LOADER_SECTORS
dw 0x1000, 0
dq 1
BOOT_DRIVE: db 0
error: db "Boot failed: disk read or less than 16 MiB RAM", 0
times 510-($-$$) db 0
dw 0xaa55
