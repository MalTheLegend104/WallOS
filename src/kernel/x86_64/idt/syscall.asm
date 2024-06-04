bits 64

global syscall_handler
extern syscall_c_hook
extern kalloc

; This represents this struct.
; We pass a pointer to this struct when calling syscall_c_hook
; typedef struct {
; 	uint64_t rax; // syscall
; 	uint64_t rdi; // arg1
; 	uint64_t rsi; // arg2
; 	uint64_t rdx; // arg3
; 	uint64_t rcx; // arg4
; 	uint64_t r8;  // arg5
; 	uint64_t r9;  // arg6
; } __attribute__((packed)) registers_t;

section .data
struc syscall_info
	.syscall_num resq 1
	.arg1		 resq 1
	.arg2		 resq 1
	.arg3		 resq 1
	.arg4		 resq 1
	.arg5		 resq 1
	.arg6		 resq 1
endstruc

; This is a little cursed.
; It allocates the struct dynamically, in an attempt to future proof for multithreading.
; It is always freed in the c-code before it returns
section .text
syscall_handler:
	; We push all the registers we care about to the stack before calling kalloc
	; This way, we dont have to worry about whatever kalloc does to the registers.
	push rax
	push rdi
	push rsi
	push rdx
	push rcx
	push r8
	push r9

	; GCC is nice, it follows System V ABI (the same one we use for our syscalls)
	mov rdi, syscall_info_size
	call kalloc
	mov r10, rax

	pop r9
	pop r8
	pop rcx
	pop rdx
	pop rsi
	pop rdi
	pop rax

	; All the registers are what they were previously, with r10 containing the struct base address
	mov QWORD [r10 + syscall_info.syscall_num], rax
	mov QWORD [r10 + syscall_info.arg1], rdi
	mov QWORD [r10 + syscall_info.arg2], rsi
	mov QWORD [r10 + syscall_info.arg3], rdx
	mov QWORD [r10 + syscall_info.arg4], rcx
	mov QWORD [r10 + syscall_info.arg5], r8
	mov QWORD [r10 + syscall_info.arg6], r9
	
	lea rdi, [r10]
	call syscall_c_hook

	iretq