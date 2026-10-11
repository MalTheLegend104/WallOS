#include <memory/kernel_alloc.h>
#include <stdint.h>
#include <system/gdt.h>

#include <string.h>

void gdt_fill(gdt_entry_t* gdt, tss_64_t* tss) {
	// 7 slots: 0=Null, 1=KCode, 2=KData, 3=UData, 4=UCode64, 5=TSS_Low, 6=TSS_High
	memset(gdt, 0, sizeof(gdt_entry_t) * GDT_SLOTS);

	gdt[0] = (gdt_entry_t) {0, 0, 0, 0, 0, 0};       // Null
	gdt[1] = (gdt_entry_t) {0, 0, 0, 0x9A, 0x20, 0}; // Kernel code (0x08)
	gdt[2] = (gdt_entry_t) {0, 0, 0, 0x92, 0x00, 0}; // Kernel data (0x10)
	gdt[3] = (gdt_entry_t) {0, 0, 0, 0xF2, 0x00, 0}; // User data   (0x18) present, DPL 3, writable
	gdt[4] = (gdt_entry_t) {0, 0, 0, 0xFA, 0x20, 0}; // User code64 (0x20) present, DPL 3, L=1

	// 16-byte TSS descriptor at slot 5 (0x28)
	uint64_t tss_addr = (uint64_t) tss;
	gdt[5].limit_low = (sizeof(tss_64_t) - 1) & 0xFFFF;
	gdt[5].base_low = tss_addr & 0xFFFF;
	gdt[5].base_middle = (tss_addr >> 16) & 0xFF;
	gdt[5].access = 0x89; // Present, 64-bit TSS (available)
	gdt[5].granularity = 0x00;
	gdt[5].base_high = (tss_addr >> 24) & 0xFF;

	gdt_entry_high_t* tss_high = (gdt_entry_high_t*) &gdt[6];
	tss_high->base_upper = (tss_addr >> 32) & 0xFFFFFFFF;
	tss_high->reserved = 0;
}

tss_64_t* set_gdt_and_tss(void) {
	// We need to set up a proper GDT on each AP.
	// There isn't a whole lot of requirement in this, GDT isn't used much in long mode
	// We need the TSS for interrupts though, and for ring 3 traps to find a kernel stack.

	gdt_entry_t* gdt = kalloc(sizeof(gdt_entry_t) * GDT_SLOTS);
	tss_64_t* tss = kalloc(sizeof(tss_64_t));
	memset(tss, 0, sizeof(tss_64_t));

	// Setup TSS Stack
	// RSP0 is used when an interrupt occurs in Ring 3.
	// The scheduler overwrites this with the running task's kernel stack top on every task switch (arch_task_switch).
	// This one only covers the window before the first switch.
	tss->rsp0 = (uint64_t) kalloc(8192) + 8192;
	tss->iopb_offset = sizeof(tss_64_t); // Point beyond TSS to disable IO bitmap

	gdt_fill(gdt, tss);

	// Load GDT
	// Don't ask me why I did it this way. Don't question it...
	gdt_ptr_t gp = {.limit = (sizeof(gdt_entry_t) * GDT_SLOTS) - 1, .base = (uint64_t) gdt};
	__asm__ volatile("lgdt %0" : : "m"(gp));

	// Reload Segments
	// GS/FS are deliberately NOT reloaded, that would clobber their MSR bases
	__asm__ volatile(
		"push $0x08\n"
		"lea 1f(%%rip), %%rax\n"
		"push %%rax\n"
		"lretq\n"
		"1:\n"
		"mov $0x10, %%ax\n"
		"mov %%ax, %%ds\n"
		"mov %%ax, %%es\n"
		"mov %%ax, %%ss\n"
		: : : "rax", "memory"
	);

	// Load Task Register
	__asm__ volatile("ltr %%ax" : : "a"(GDT_SEL_TSS));
	return tss;
}
