[org 0x7c00]

KERNEL_OFFSET equ 0x1000

mov [BOOT_DRIVE], dl	; BIOS stores boot drive in dl

mov bp, 0x9000		; set up stack
mov sp, bp

mov bx, MSG_REAL_MODE
call print_string

call load_kernel	; this uses the 16-bit disk load function

call switch_to_pm

jmp $

%include "boot/include/print_string.asm"
%include "boot/include/disk_load.asm"
%include "boot/include/print_string_pm.asm"
%include "boot/include/gdt.asm"
%include "boot/include/switch_to_pm.asm"

[bits 16]

load_kernel:
	mov bx, MSG_LOAD_KERNEL
	call print_string

	mov bx, KERNEL_OFFSET	; load to address KERNEL_OFFSET (0x1000)
	mov dh, 13	; load up 13 sectors after the boot sector
	mov dl, [BOOT_DRIVE]
	call disk_load

	ret

[bits 32]

BEGIN_PM:
	call cls

	mov ebx, MSG_PROT_MODE
	call print_string_pm

	call KERNEL_OFFSET

	jmp $

; variables
BOOT_DRIVE: db 0
MSG_REAL_MODE: db "Started in 16-bit Real Mode", 0xa, 0xd, 0
MSG_PROT_MODE: db "Successfully landed in 32-bit Protected Mode", 0xa, 0xd, 0
MSG_LOAD_KERNEL: db "Loading kernel into memory", 0xa, 0xd, 0

; boot sector padding
times 510-($-$$) db 0
dw 0xaa55



