[bits 16]
; switch to protected mode
switch_to_pm:
	cli			; switch off interrupts
	lgdt [gdt_descriptor]	; load new global descriptor table
	mov eax, cr0		; to switch to protected mode, set first bit of cr0 (a control register)
	or eax, 0x1
	mov cr0, eax	

	jmp CODE_SEG:init_pm	; make a far jump (to a new segment), forces CPU to flush all cache

[bits 32]
init_pm:
	mov ax, DATA_SEG	; old segments are not meaningless, point all segment register to data selector defined in the gdt
	mov ds, ax		
	mov ss, ax
	mov es, ax
	mov fs, ax
	mov gs, ax

	mov ebp, 0x90000	; update stack position so it is right on top of the free space
	mov esp, ebp

	call BEGIN_PM		; call some well known label
