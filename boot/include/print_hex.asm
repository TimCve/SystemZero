print_hex:
	pusha

	; first digit
	mov cx, dx
	shr cx, 12
	call convert_ascii
	mov byte [hex_out+2], cl

	; second digit
	mov cx, dx
	and cx, 0x0f00
	mov cl, ch
	call convert_ascii
	mov byte [hex_out+3], cl

	; third digit
	mov cx, dx
	shr cl, 4
	call convert_ascii
	mov byte [hex_out+4], cl

	; fourth digit
	mov cx, dx
	and cx, 0x000f
	call convert_ascii
	mov byte [hex_out+5], cl

	mov bx, hex_out
	call print_string

	popa 
	ret

convert_ascii:
	cmp cl, 0x9
	jg letter

	number:
		add cl, 0x30
		jmp return
	letter:
		add cl, 0x37
	return:
		ret
