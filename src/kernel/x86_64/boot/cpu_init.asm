[BITS 16]

section .bootstrap.data

gdt_start:
    dq 0x0000000000000000        ; null

    ; 32-bit code
    dq 0x00CF9A000000FFFF

    ; 32-bit data
    dq 0x00CF92000000FFFF

    ; 64-bit code
    dq 0x00209A0000000000

    ; 64-bit data
    dq 0x0000920000000000

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

pml4_pointer: dq 0
global pml4_pointer

ap_stack_pointer: dq 0
global ap_stack_pointer


section .bootstrap.text
align 16

real_mode_trampoline_entry:
	cli

	; Set up temp stack
	mov ax, 0x0000
    mov ss, ax
    mov sp, 0x7C00

	; Load the GDT
	lgdt [gdt_descriptor]

	; Enable protected mode
	mov eax, cr0
    or eax, 1
    mov cr0, eax

	; Long jump to protected mode
    jmp 0x08:protected_mode_entry


[BITS 32]

protected_mode_entry:

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Enable PAE
    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    ; Load PML4 (must be provided by BSP)
    mov eax, [pml4_pointer]
    mov cr3, eax

    ; Enable long mode
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr

    ; Enable paging
    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    jmp 0x18:long_mode_entry

[BITS 64]

extern x86_ap_main
long_mode_entry:

    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Load AP stack (set by BSP before SIPI)
    mov rsp, [ap_stack_pointer]

    ; Call C entry
    call x86_ap_main

hang:
    hlt
    jmp hang


