[BITS 32]
global start
extern kernel_main

KERNEL_VIRTUAL_BASE equ 0xFFFFFFFF80000000
KERNEL_BASE_PML4_INDEX equ (((KERNEL_VIRTUAL_BASE) >> 39) & 0x1FF)
KERNEL_BASE_PDPT_INDEX equ  (((KERNEL_VIRTUAL_BASE) >> 30) & 0x1FF)

section .boot.data
multiboot_data_magic:     dq 0
multiboot_data_address:   dq 0

align 16
GDT64:                           
	.null: equ $ - GDT64         ; The null descriptor.
	dq 0
	.Code: equ $ - GDT64         ; The code descriptor.
	dw 0                         ; Limit (low).
	dw 0                         ; Base (low).
	db 0                         ; Base (middle)
	db 10011010b                 ; Access (exec/read).
	db 00100000b                 ; Granularity, 64 bits flag, limit19:16.
	db 0                         ; Base (high).
	.Data: equ $ - GDT64         ; The data descriptor.
	dw 0                         ; Limit (low).
	dw 0                         ; Base (low).
	db 0                         ; Base (middle)
	db 10010010b                 ; Access (read/write).
	db 00000000b                 ; Granularity.
	db 0                         ; Base (high).
GDT64Pointer:                    ; The GDT-pointer.
	dw $ - GDT64 - 1             ; Limit.
	dq GDT64                     ; Base.


align 4096
kernel_pml4:
times 512 dq 0

align 4096
kernel_pde:
times 512 dq 0

align 4096
kernel_pdpt:
dq 0
times 511 dq 0

align 4096
kernel_pdpt2:
times KERNEL_BASE_PDPT_INDEX dq 0
dq 0

section .boot.text
; Make sure this is an x86_64 CPU
check_cpuid:
	pushfd
	pop eax
	mov ecx, eax
	xor eax, 1 << 21
	push eax
	popfd
	pushfd
	pop eax
	push ecx
	popfd
	cmp eax, ecx
	je .no_cpuid
	ret
.no_cpuid:
	mov al, "C"
	jmp error

check_long_mode:
	mov eax, 0x80000000
	cpuid
	cmp eax, 0x80000001
	jb .no_long_mode

	mov eax, 0x80000001
	cpuid
	test edx, 1 << 29
	jz .no_long_mode
	
	ret
.no_long_mode:
	mov al, "L"
	jmp error

; Sets up paging
setup_page_tables:
	mov ecx, 512
	mov eax, kernel_pde
	mov ebx, 0x83
.fill_pde:
	mov dword [eax], ebx
	add ebx, 0x200000 ; Go to next 2M
	add eax, 8
	loop .fill_pde

	mov eax, kernel_pdpt ; Get address of PDPT
	or eax, 3 ; Present, Write
	mov dword [kernel_pml4], eax

	mov eax, kernel_pdpt2 ; Second PDPT
	or eax, 3
	mov dword [kernel_pml4 + KERNEL_BASE_PML4_INDEX * 8], eax

	mov eax, kernel_pde ; Second PDPT
	or eax, 3
	mov dword [kernel_pdpt], eax
	mov dword [kernel_pdpt2 + KERNEL_BASE_PDPT_INDEX * 8], eax

	; Put the base pointer in cr3
	mov eax, kernel_pml4
	mov cr3, eax
	ret

enable_paging:
	; enable PAE
	mov eax, cr4
	or eax, 1 << 5
	mov cr4, eax
	
	; enable long mode
	mov ecx, 0xC0000080
	rdmsr
	or eax, 1 << 8
	wrmsr

	; enable paging
	mov eax, cr0
	or eax, 1 << 31
	mov cr0, eax

	ret

error:
	; print "ERR: X" where X is the error code
	mov dword [0xb8000], 0x4f524f45
	mov dword [0xb8004], 0x4f3a4f52
	mov dword [0xb8008], 0x4f204f20
	mov byte  [0xb800a], al
	hlt

start:
	; Move multiboot pointer to ebx (assuming it exists)
	; We'll check for the header in kernel main
	mov DWORD [multiboot_data_magic],    eax
	mov DWORD [multiboot_data_address],  ebx
	
	; Make sure we have a x86_64 processor and it has long mode
	call check_cpuid
	call check_long_mode
	
	; Set up our base page tables.
	call setup_page_tables
	call enable_paging

	lgdt [GDT64Pointer]
  	jmp 0x8:long_mode_start - KERNEL_VIRTUAL_BASE

	cli
	hlt


[BITS 64]

extern _bss_start_
extern _bss_end_

section .data
GDT64Pointer64:                    ; The GDT-pointer.
    dw GDT64Pointer - GDT64 - 1    ; Limit.
    dq GDT64 + KERNEL_VIRTUAL_BASE ; Base.

section .text
long_mode_start:
	lgdt [GDT64Pointer64]

   	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

	; Zero uninitialized memory so there's no junk
	mov rdi, _bss_start_
	mov rcx, _bss_end_
	sub rcx, _bss_start_
	xor rax, rax
	rep stosb

	mov rsp, stack_top

	mov edi, DWORD[multiboot_data_magic]
    mov esi, DWORD[multiboot_data_address]
	call kernel_main
    hlt

section .bss
align 4096
stack_bottom:
	resb 32768
stack_top:
