[BITS 32]
global start


KERNEL_VIRTUAL_BASE equ 0xFFFFFFFF80000000

section .boot.data
multiboot_data_magic:     dq 0
multiboot_data_address:   dq 0



align 16
GDT64:                           ; Global Descriptor Table (64-bit).
	.Null: equ $ - GDT64         ; The null descriptor.
	dw 0xFFFF                    ; Limit (low).
	dw 0                         ; Base (low).
	db 0                         ; Base (middle)
	db 0                         ; Access.
	db 0                         ; Granularity.
	db 0                         ; Base (high).
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
	.TSS: ;equ $ - GDT64         ; TSS Descriptor
	.len:
	dw 108                       ; TSS Length - the x86_64 TSS is 108 bytes loong
	.low:
	dw 0                         ; Base (low).
	.mid:
	db 0                         ; Base (middle)
	db 10001001b                 ; Flags
	db 00000000b                 ; Flags 2
	.high:
	db 0                         ; Base (high).
	.high32:
	dd 0                         ; High 32 bits
	dd 0                         ; Reserved
GDT64Pointer:                    ; The GDT-pointer.
	dw $ - GDT64 - 1             ; Limit.
	dq GDT64                     ; Base.

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
	mov eax, page_table_l3
	or eax, 0b11 ; present, writable
	mov [page_table_l4], eax
	
	mov eax, page_table_l2
	or eax, 0b11 ; present, writable
	mov [page_table_l3], eax

	mov ecx, 0 ; counter
.loop:

	mov eax, 0x200000 ; 2MiB
	mul ecx
	or eax, 0b10000011 ; present, writable, huge page
	mov [page_table_l2 + ecx * 8], eax

	inc ecx ; increment counter
	cmp ecx, 512 ; checks if the whole table is mapped
	jne .loop ; if not, continue

	ret
; TODO, WE NEED TO FILL THE NEW PAGE TABLE STRUCTURE
; I DIDNT GET AROUND TO IT BEFORE THE COMMIT
enable_paging:
	; pass page table location to cpu
	mov eax, page_table_l4
	mov cr3, eax

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
  	jmp 0x8:entry64 - KERNEL_VIRTUAL_BASE

	hlt


[BITS 64]

extern _bss
extern _bss_end

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

	mov rdi, _bss
	mov rcx, _bss_end
	sub rcx, _bss
	xor rax, rax
	rep stosb

	mov rsp, stack_top

	mov rdi, DWORD[multiboot_data_magic]
    mov rsi, DWORD[multiboot_data_address]
	call kernel_main
    hlt

section .bss
align 4096
stack_bottom:
	resb 32768
stack_top:
