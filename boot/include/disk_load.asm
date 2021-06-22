; will load dh sectors into es:bx from drive dl

disk_load:
	push dx

	mov ah, 0x02	; BIOS disk read routine
	mov al, dh	; read dh sectors
	mov ch, 0x00	; select cylinder 0
	mov dh, 0x00 	; select head 0
	mov cl, 0x02	; start read from 2nd sector (after boot sector)

	int 0x13	; BIOS disk read interrupt

	jc disk_error	; if carry flag is set (error), jump to disk_error

	pop dx
	cmp dh, al	; al (sectors read) must equal dh (sectors expected to read)
	jne disk_error
	ret

disk_error:
	mov bx, disk_error_message
	call print_string
	jmp $

disk_error_message: db "Disk read error!", 0xa, 0xd, 0
