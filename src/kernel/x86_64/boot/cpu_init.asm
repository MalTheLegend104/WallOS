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


idtr_emergency:
    dw (32 * 16) - 1   ; Limit (32 entries, 16 bytes each)
    dq 0               ; Base (we will patch this at runtime)

align 16
emergency_idt:
    times 32 * 16 db 0 ; Space for 32 descriptors

section .bootstrap.text
align 16

; real_mode_trampoline_entry:
; 	cli

;     xor ax, ax
;     mov ds, ax
;     mov es, ax

; 	; Set up temp stack
; 	mov ax, 0x0000
;     mov ss, ax
;     mov sp, 0x7C00

; 	; Load the GDT
; 	lgdt [gdt_descriptor]

; 	; Enable protected mode
; 	mov eax, cr0
;     or eax, 1
;     mov cr0, eax

; 	; Long jump to protected mode
;     jmp 0x08:protected_mode_entry

real_mode_trampoline_entry:
    cli
    
    ; Setup segments to 0 for linear addressing
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; --- THE FIX: Calculate Physical GDT Address ---
    ; We know this code is at 0x8000. 
    ; We calculate: 0x8000 + (gdt_start - real_mode_trampoline_entry)
    mov eax, 0x8000
    add eax, gdt_start - real_mode_trampoline_entry
    mov [gdt_descriptor + 2], eax ; Patch the 'dd' in the descriptor below

    lgdt [gdt_descriptor]

    ; Enable protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Long jump to 32-bit (0x08 is your 32-bit code segment)
    ; Using a hardcoded jump to ensure we hit the right physical address
    jmp 0x08:0x8000 + (protected_mode_entry - real_mode_trampoline_entry)


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

    ; This is more likely to not hate me than trying jmp 0x18:long_mode_entry
    push 0x18
    push long_mode_entry
    retf
    
[BITS 64]

; Common macro for exceptions
%macro EXCEPTION_STUB 1
align 16
exception_stub_%1:
    push qword %1    ; Push vector number
    jmp common_exception_handler
%endmacro

; Generate stubs for the first 32 exceptions
%assign i 0
%rep 32
    EXCEPTION_STUB i
%assign i i+1
%endrep

common_exception_handler:
    ; The stack has the vector number at [rsp]
    pop rax          ; Get vector number into RAX
    
    ; Convert low nibble to hex character
    and al, 0x1F     ; Keep it 0-31
    cmp al, 10
    jl .is_digit
    add al, 7
.is_digit:
    add al, 0x30
    mov bl, al       ; Save the hex char in BL for a moment

    mov dx, 0x3F8

    ; Print 'E'
    mov al, 'E'
    out dx, al

    ; Print ':'
    mov al, ':'
    out dx, al

    ; Print the Vector Hex Char (stored in BL)
    mov al, bl
    out dx, al

    ; Print Newline (\n)
    mov al, 10
    out dx, al

    hlt
    jmp $

extern x86_ap_main
long_mode_entry:

    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Load AP stack (set by BSP before SIPI)
    mov rsp, [ap_stack_pointer]

    mov rsi, 0x8000 + (exception_stub_0 - real_mode_trampoline_entry)
    mov rdi, 0x8000 + (emergency_idt - real_mode_trampoline_entry)
    mov rcx, 32

.loop:
    mov [rdi], si             ; Offset 0..15
    mov word [rdi + 2], 0x18  ; CS
    mov word [rdi + 4], 0x8E00; Attributes
    
    ; High bits of offset
    mov rbx, rsi
    shr rbx, 16
    mov [rdi + 6], bx         ; Offset 16..31
    shr rbx, 16
    mov [rdi + 8], ebx        ; Offset 32..63
    
    add rdi, 16
    add rsi, 16               ; Move to next stub (they are 16-byte aligned)
    loop .loop

    ; 3. Load the IDTR
    mov rax, 0x8000 + (emergency_idt - real_mode_trampoline_entry)
    mov [idtr_emergency + 2], rax
    lidt [0x8000 + (idtr_emergency - real_mode_trampoline_entry)]

    ; --- Enable SSE/FPU ---
    mov rax, cr0
    and ax, 0xFFFB      ; Clear CR0.EM (bit 2) - Emulator bit
    or ax, 0x2          ; Set CR0.MP (bit 1) - Monitor Coprocessor
    mov cr0, rax

    mov rax, cr4
    or rax, (1 << 9)    ; Set OSFXSR (bit 9) - FXSAVE/FXRSTOR support
    or rax, (1 << 10)   ; Set OSXMMEXCPT (bit 10) - Unmasked SIMD exceptions
    mov cr4, rax

    ; 4. NOW call the C code
    call x86_ap_main

hang:
    hlt
    jmp hang


