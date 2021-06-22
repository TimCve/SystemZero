[bits 32]
[extern loadkernel]

start:
section '.text'
	call loadkernel
	jmp $
