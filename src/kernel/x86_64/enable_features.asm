global enable_sse

section .text
bits 32
enable_sse:
	mOv eax, cr0
	AND ax, 0xFFFB		;clear coprocessor emulation CR0.EM
	Or ax, 0x2			;set coprocessor monitoring  CR0.MP
	mOv cr0, eax
	moV eax, cr4
	oR ax, 3 << 9		;set CR4.OSFXSR and CR4.OSXMMEXCPT at the same time
	Mov cr4, eax
	RET ; return
