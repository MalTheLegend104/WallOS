bits 64

section .data
; Constants for PIT frequency configuration
PIT_FREQUENCY equ 1193182 ; Base frequency of the PIT (in Hz)
DESIRED_FREQUENCY equ 1000 ; 1 ms desired frequency

; Calculate the PIT counter value
PIT_COUNTER equ PIT_FREQUENCY / DESIRED_FREQUENCY

; Port addresses for PIT
PIT_COMMAND_PORT equ 0x43
PIT_CHANNEL0_PORT equ 0x40

section .text
global enablePS2
global pic_irq_enable
global pic_irq_disable
global idt_load
global disablePIC
global enableAPIC

; We disable the 8359 PIC, otherwise it will send unwanted IRQs
; We also need to do this so we can set up the APIC, so we kill two birds with one stone

disablePIC:
		; (ICW = initialization command word)
		; Initialize the master PIC.
		mov     al,     0x11        ; ICW1: 0x11 = init with 4 ICW's
		out     0x20,   al
		mov     al,     0x20        ; ICW2: 0x20 = interrupt offset 32
		out     0x21,   al
		mov     al,     0x04        ; ICW3: 0x04 = IRQ2 has a slave
		out     0x21,   al
		mov     al,     0x01        ; ICW4: 0x01 = x86 mode
		out     0x21,   al

		; Initialize the slave PIC.
		mov     al,     0x11        ; ICW1: 0x11 = init with 4 ICW's
		out     0xa0,   al
		mov     al,     0x28        ; ICW2: 0x28 = interrupt offset 40
		out     0xa1,   al
		mov     al,     0x02        ; ICW3: 0x02 = attached to master IRQ2.
		out     0xa1,   al
		mov     al,     0x01        ; ICW4: 0x01 = x86 mode
		out     0xa1,   al

		; Disable all IRQs. The kernel will re-enable the ones it wants to handle later.
		; TODO actually re-enable the ones we need.
		mov     al,     0xff
		out     0x21,   al
		out     0xa1,   al

		ret

; Currently the APIC hates me and I want keyboard support
; we are going to re-enable some things in the PIC
pic_irq_enable:
    ; Move IRQ into cl.
    mov     rcx,    rdi
    ; Determine which PIC to update (<8 = master, else slave).
    cmp     cl,     8
    jae     .slave
    .master:
        ; Compute the mask ~(1 << IRQ).
        mov     edx,    1
        shl     edx,    cl
        not     edx
        ; Read the current mask.
        in      al,     0x21
        ; Clear the IRQ bit and update the mask.
        and     al,     dl
        out     0x21,   al
        ret
    .slave:
        ; Recursively enable master IRQ2, or else slave IRQs will not work.
        push    rcx         ; Save original IRQ number (rcx) before the recursive call
        mov     rdi,    2   ; Recursively enable master IRQ2
        call    pic_irq_enable
        pop     rcx          ; Restore original IRQ number

        ; Subtract 8 from the IRQ.
        sub     cl,     8
        ; Compute the mask ~(1 << IRQ).
        mov     edx,    1
        shl     edx,    cl
        not     edx
        ; Read the current mask.
        in      al,     0xa1
        ; Clear the IRQ bit and update the mask.
        and     al,     dl
        out     0xa1,   al
        ret

; Disable IRQs if I ever get to the APIC
pic_irq_disable:
    ; Move IRQ into cl.
    mov     rcx,    rdi
    ; Determine which PIC to update (<8 = master, else slave).
    cmp     cl,     8
    jae     .slave
    .master:
        ; Compute the mask (1 << IRQ).
        mov     edx,    1
        shl     edx,    cl
        ; Read the current mask.
        in      al,     0x21
        ; Set the IRQ bit and update the mask.
        or      al,     dl
        out     0x21,   al
        ret

    .slave:
        ; Subtract 8 from the IRQ.
        sub     cl,     8
        ; Compute the mask (1 << IRQ).
        mov     edx,    1
        shl     edx,    cl
        ; Read the current mask.
        in      al,     0xa1
        ; Set the IRQ bit and update the mask.
        or      al,     dl
        out     0xa1,   al
        ret

idt_load:
	cli ; clears the interrupts, kinda unecessary but whatever
	lidt [rdi]
	sti
	ret

; ------------------------------------------------------------------------------------------------
; Interrupt/Exception entry
;
; All 256 vectors enter through an individual stub that pushes the vector number and error code, then jump to the main handler
; The main handler will swapgs if we came from user mode, push all GPRs, then call x86_interrupt_dispatch
; After that returns, we pop all GPRs, remove the vector and error code from the stack, swapgs if we are returning to ring 3, then iretq
;
; Frame, low -> high. This MUST match `struct interrupt_regs` in idt.h:
;     r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax | vector | error_code | rip cs rflags rsp ss
;
; The test for user mode is on the CS of the interrupted frame, and the exit path tests the CS of the frame being RETURNED to.
; This means a handler is free to edit regs->cs/rip/etc.
;
; The scheduler handles it's own interrupts, overriding the vector stub that calls main.
; Same for syscall/sysret.
;
; Technically we don't have anything for the IST stacks for NMI, #DF, or #MC yet, but like, not really that big of a deal to me
; It'll just triple fault and I don't really care if that happens on a hardware error... (#DF is different but still not really something I want to deal with)
; ------------------------------------------------------------------------------------------------
extern x86_interrupt_dispatch

section .text

x86_isr_common:                     ; stack: [vector][error][rip][cs]...
	test byte [rsp + 24], 3         ; CS of the interrupted code (RPL bits)
	jz   .from_kernel
	swapgs
.from_kernel:
	push rax
	push rbx
	push rcx
	push rdx
	push rsi
	push rdi
	push rbp
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15

	mov  rdi, rsp                   ; arg0 = struct interrupt_regs*
	mov  rbp, rsp                   ; rbp is callee-saved, so it survives the call
	and  rsp, -16                   ; SysV requires that stack is 16-byte aligned at the call
	cld                             ; SysV needs DF=0. iretq restores the interrupted code's RFLAGS
	call x86_interrupt_dispatch
	mov  rsp, rbp

	pop  r15
	pop  r14
	pop  r13
	pop  r12
	pop  r11
	pop  r10
	pop  r9
	pop  r8
	pop  rbp
	pop  rdi
	pop  rsi
	pop  rdx
	pop  rcx
	pop  rbx
	pop  rax
	add  rsp, 16                    ; remove the vector + error code from the stack

	test byte [rsp + 8], 3          ; CS of the frame we are returning to
	jz   .to_kernel
	swapgs
.to_kernel:
	iretq

; Below is awful macro magic for NASM
; It makes all the stubs for all the interrupts, pushing 0 as the error code for the interrupts that doesn't have them
; Adds them all to a table so we can load it using lidt

; CPU exceptions that automatically push an error code:
; #DF (8), #TS (10), #NP (11), #SS (12), #GP (13), #PF (14), #AC (17), #CP (21), #VC (29), #SX (30).
;
; Generate an ISR stub for each interrupt vector (0-255).
%macro ISR_STUB 1
x86_isr_stub_ %+ %1:
%if %1 != 8 && %1 != 10 && %1 != 11 && %1 != 12 && %1 != 13 && %1 != 14 && %1 != 17 && %1 != 21 && %1 != 29 && %1 != 30
	push qword 0 ; fake error code for interrupts that don't have them, so every frame looks the same
%endif

	; Push the vector number so the common handler can identify the ISR.
	push qword %1
	jmp x86_isr_common
%endmacro

; This is what actually makes the stubs
%assign v 0    ; inits NASM preprocessor variable v to zero
%rep 256       ; repeat 256 times
	ISR_STUB v ; Makes all the interrupts using the macro
%assign v v+1  ; v++
%endrep        ; end repeat

section .data

global x86_isr_stub_table

x86_isr_stub_table:
	; Export a lookup table of stub addresses, indexed by interrupt vector.
	; for (int v = 0; v < 256; v++) x86_isr_stub_table[v] = x86_isr_stub_<v>
%assign v 0
%rep 256
	dq x86_isr_stub_ %+ v
%assign v v+1
%endrep

; GCC was complaining about this
section .note.GNU-stack noalloc noexec nowrite progbits