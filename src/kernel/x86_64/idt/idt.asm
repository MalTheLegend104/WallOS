bits 64

section .text
global idt_load

idt_load:
	; We disable the 8359 PIC, otherwise it will send unwanted IRQs
	; We also need to do this so we can set up the APIC, so we kill two birds with one stone
	.setupPIC:
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

	; Now we actually get to load the idt
	.load_idt:
		cli ; clears the interrupts, kinda unecessary but whatever
		lidt [rdi]
		sti
		ret
