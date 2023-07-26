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
global irq_enable
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
irq_enable:
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
        mov     rdi,    2
        call    irq_enable
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
irq_disable:
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

; This is broken
enableAPIC:
	; Set the APIC enable bit (bit 11) in the IA32_APIC_BASE MSR
	rdmsr                      ; Read the IA32_APIC_BASE MSR into EDX:EAX.
	or     eax, (1 << 11)      ; Set bit 11 to enable the APIC.
	wrmsr                      ; Write the modified value back to IA32_APIC_BASE MSR.

	; Enable the x2APIC by setting the x2APIC_ENABLE bit (bit 10) in IA32_APIC_BASE MSR.
	rdmsr                      ; Read the IA32_APIC_BASE MSR into EDX:EAX.
	or     eax, (1 << 10)      ; Set bit 10 to enable x2APIC.
	wrmsr                      ; Write the modified value back to IA32_APIC_BASE MSR.

	mov     ecx, 0x21         ; The interrupt vector (ISR 0x21) we want to map the keyboard to.
	mov     eax, 1 << 16      ; Set the Delivery Mode to "Fixed" (bits 8-10 = 0b000) and
	or      eax, 1 << 11      ; Set the Destination Mode to "Physical" (bit 11 = 1).
	mov     edx, 1 << 24      ; Set bit 24 to enable the interrupt (IA32_APIC_LVT_MASK).
	wrmsr

enablePS2:
	ret

global reEnableIRQ1
reEnableIRQ1:
	cli ; Clear interrupts
    ; Call irq_enable with argument 1 to enable IRQ1
    mov     rdi,    1
    call    irq_enable
    ; Map IRQ1 to interrupt vector 0x21 (interrupt offset 33).
    ; mov     al,     0x21        ; ICW2: 0x21 = interrupt offset 33
    ; out     0x21,   al 			; IRQ 1 is handled by the master pic
	sti ; re-enable interrupts
    ret

idt_load:
	cli ; clears the interrupts, kinda unecessary but whatever
	lidt [rdi]
	sti
	ret
