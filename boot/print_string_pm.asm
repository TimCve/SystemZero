[bits 32]

VIDEO_MEMORY equ 0xb8000
WHITE_ON_BLACK equ 0x0f

cls:
	pusha

	mov eax, 0
	mov edx, VIDEO_MEMORY

	cls_loop:
		cmp eax, 4000
		je cls_done
		mov word [edx], 0x0
		add edx, 1
		add eax, 1
		jmp cls_loop

	cls_done:
		popa
		ret

print_string_pm:
	pusha
	mov edx, VIDEO_MEMORY

print_string_pm_loop:
	mov al, [ebx]
	mov ah, WHITE_ON_BLACK

	cmp al, 0
	je print_string_pm_done

	mov [edx], ax

	add ebx, 1
	add edx, 2

	jmp print_string_pm_loop

print_string_pm_done:
	popa
	ret
