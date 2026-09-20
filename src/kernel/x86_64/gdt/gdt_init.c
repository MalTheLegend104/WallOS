#include <stdint.h>
#include <system/gdt.h>
#include <memory/kernel_alloc.h>

#include <string.h>

void set_ap_gdt_and_tss() {
	// We need to set up a proper GDT on each AP.
	// There isn't a whole lot of requirement in this, GDT isn't used much in long mode
	// We need the TSS for interrupts though.

	// 5 slots: 0=Null, 1=Code, 2=Data, 3=TSS_Low, 4=TSS_High
	struct gdt_entry* gdt = kalloc(sizeof(struct gdt_entry) * 5);
	struct tss_64* tss = kalloc(sizeof(struct tss_64));
	memset(tss, 0, sizeof(struct tss_64));

	// Setup Standard Segments
	gdt[0] = (struct gdt_entry){ 0, 0, 0, 0, 0, 0 };           // Null
	gdt[1] = (struct gdt_entry){ 0, 0, 0, 0x9A, 0x20, 0 };     // Code (0x08)
	gdt[2] = (struct gdt_entry){ 0, 0, 0, 0x92, 0x00, 0 };     // Data (0x10)

	// Setup TSS Stack
	// RSP0 is used when an interrupt occurs in Ring 3. 
	// Even in Ring 0, having a valid TSS is an architectural requirement.
	tss->rsp0 = (uint64_t) kalloc(8192) + 8192;
	tss->iopb_offset = sizeof(struct tss_64); // Point beyond TSS to disable IO bitmap

	// Setup 16-byte TSS Descriptor in GDT (Starting at 0x18)
	uint64_t tss_addr = (uint64_t) tss;
	gdt[3].limit_low = (sizeof(struct tss_64) - 1) & 0xFFFF;
	gdt[3].base_low = tss_addr & 0xFFFF;
	gdt[3].base_middle = (tss_addr >> 16) & 0xFF;
	gdt[3].access = 0x89; // Present, Executable, TSS Type
	gdt[3].granularity = 0x00;
	gdt[3].base_high = (tss_addr >> 24) & 0xFF;

	struct gdt_entry_high* tss_high = (struct gdt_entry_high*) &gdt[4];
	tss_high->base_upper = (tss_addr >> 32) & 0xFFFFFFFF;
	tss_high->reserved = 0;

	// Load GDT
	// Don't ask me why I did it this way. Don't question it...
	struct gdt_ptr gp = { .limit = (sizeof(struct gdt_entry) * 5) - 1, .base = (uint64_t) gdt };
	__asm__ volatile("lgdt %0" : : "m"(gp));

	// Reload Segments
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
	__asm__ volatile("ltr %%ax" : : "a"(0x18));
}