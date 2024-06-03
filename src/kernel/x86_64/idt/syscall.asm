bits 64

global syscall_handler
extern syscall_c_hook

section .text
syscall_handler:
	; mov word [syscall_num], ax
	; mov rdi, rax
	call syscall_c_hook
	iretq
