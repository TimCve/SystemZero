print_string:
	pusha
	mov ah, 0x0e	
  	print:
		mov al, [bx]
		cmp byte al, 0
		je finish
		int 0x10
		inc bx
		jmp print
	finish:
		popa
		ret
