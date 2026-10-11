#include <cpu_io.h>
#include <drivers/keyboard.h>
#include <drivers/serial.h>
#include <klibc/logger.h>
#include <panic.h>
#include <scheduler/scheduler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <system/idt.h>
#include <wallos_attributes.h>

// I aint touching the interrupt frame on 99% of these but it's required by gcc.
#pragma GCC diagnostic ignored "-Wunused-parameter"

// Define the structure of an IDT entry
struct idt_entry {
	uint16_t offset_low;
	uint16_t selector;
	uint8_t ist;
	uint8_t type_attr;
	uint16_t offset_mid;
	uint32_t offset_high;
	uint32_t zero;
} __attribute__((packed));

// Define the structure of the IDT descriptor
struct idt_descriptor {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed));

// Define the IDT array
struct idt_entry idt[256];
struct idt_descriptor idt_desc;


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Abstraction for EOI.
// We need LAPIC EOI if using IOAPIC, regular PIC EOI otherwise.
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#include <x86_64/lapic.h>
// Do we currently accept interrupts from the PIC?
// False = Legacy PIC mode, True = IOAPIC/LAPIC mode.
bool pic_disabled = false;

void interrupt_eoi(uint8_t irq_number) {
	if (pic_disabled) {
		// Local APIC EOI is just a write of 0 to the EOI register
		lapic_write(LAPIC_EOI, 0);
	} else {
		// Legacy PIC EOI
		if (irq_number >= 8) {
			outb(0xA0, 0x20); // Slave
		}
		outb(0x20, 0x20); // Master
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// IRQ handling
// Bringing up other APs means we need to keep track of how we use the legacy PIC.
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#include <x86_64/ioapic.h>

extern void pic_irq_enable(uint8_t irq);
extern void pic_irq_disable(uint8_t irq);

#define IRQ_LEGACY_COUNT 16
#define IRQ_CASCADE      2 /* PIC master->slave link, no meaning on an IOAPIC */
#define IRQ_VECTOR_BASE  32 /* ISA IRQ n arrives on vector 32 + n */

static volatile uint16_t irq_enabled_mask; /* bit n = ISA IRQ n is wanted */
static volatile uint32_t irq_dest_apic; /* LAPIC id that receives IOAPIC-routed IRQs */

bool irq_is_enabled(uint8_t irq) {
	if (irq >= IRQ_LEGACY_COUNT) return false;
	return (__atomic_load_n(&irq_enabled_mask, __ATOMIC_ACQUIRE) >> irq) & 1u;
}

void irq_enable(uint8_t irq) {
	if (irq >= IRQ_LEGACY_COUNT) return;
	__atomic_fetch_or(&irq_enabled_mask, (uint16_t) (1u << irq), __ATOMIC_ACQ_REL);

	if (!pic_disabled) pic_irq_enable(irq);
	else if (irq != IRQ_CASCADE) ioapic_route_irq(irq, IRQ_VECTOR_BASE + irq, irq_dest_apic, false);
}

void irq_disable(uint8_t irq) {
	if (irq >= IRQ_LEGACY_COUNT) return;
	__atomic_fetch_and(&irq_enabled_mask, (uint16_t) ~(1u << irq), __ATOMIC_ACQ_REL);

	if (!pic_disabled) pic_irq_disable(irq);
	else if (irq != IRQ_CASCADE) ioapic_mask_irq(irq);
}

void irq_route_enabled_to_ioapic(uint32_t dest_apic_id) {
	irq_dest_apic = dest_apic_id;

	for (uint8_t irq = 0; irq < IRQ_LEGACY_COUNT; irq++) {
		if (irq == IRQ_CASCADE || !irq_is_enabled(irq)) continue;
		ioapic_route_irq(irq, IRQ_VECTOR_BASE + irq, dest_apic_id, false);
		printf_serial(
			"[IOAPIC] IRQ %u routed to vector %u on APIC %u\r\n",
			irq,
			IRQ_VECTOR_BASE + irq,
			dest_apic_id
		);
	}
}


/* A few IRQs are required to be level triggered rather than edge triggered.
 * This is currently only used by ACPI but I want this abstraction in case I need it again.
 */

void irq_set_level_triggered(uint8_t irq) {
	if (irq > 15) {
		return; // The 8259 PIC only handles IRQs 0-15
	}

	uint16_t port = (irq < 8) ? 0x4D0 : 0x4D1;
	uint8_t bit = irq % 8;

	uint8_t elcr = inb(port);
	outb(port, elcr | (1 << bit));
}

void irq_set_edge_triggered(uint8_t irq) {
	if (irq > 15) {
		return;
	}

	uint16_t port = (irq < 8) ? 0x4D0 : 0x4D1;
	uint8_t bit = irq % 8;

	uint8_t elcr = inb(port);
	outb(port, elcr & ~(1 << bit));
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Interrupt dispatch
// Every vector enters through idt.asm, which calls x86_interrupt_dispatch().
// Dispatch looks up our local registry of interrupts.
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// The asm side depends on this layout
// This is just an effort to attempt to *not* break the interrupt handler in the futurer
_Static_assert(offsetof(struct interrupt_regs, rax) == 14 * 8, "interrupt_regs: GPR block moved");
_Static_assert(offsetof(struct interrupt_regs, vector) == 15 * 8, "interrupt_regs: vector slot moved");
_Static_assert(offsetof(struct interrupt_regs, error_code) == 16 * 8, "interrupt_regs: error code slot moved");
_Static_assert(offsetof(struct interrupt_regs, rip) == 17 * 8, "interrupt_regs: hardware frame moved");
_Static_assert(offsetof(struct interrupt_regs, cs) == 18 * 8, "idt.asm tests CS at [rsp + 24] on entry and [rsp + 8] on exit");
_Static_assert(sizeof(struct interrupt_regs) == 22 * 8, "interrupt_regs: size");

// Has all the registered interrupt handlers, these are called through dispatch, shared on all CPUs
static interrupt_handler_t int_handlers[256];

/* Debug function to dump all registers from the frame */
static void dump_regs(const struct interrupt_regs* r) {
	printf_serial("\tvector=%llu error=0x%llx\r\n", r->vector, r->error_code);
	printf_serial("\tRIP=0x%llx CS=0x%llx RFLAGS=0x%llx RSP=0x%llx SS=0x%llx\r\n", r->rip, r->cs, r->rflags, r->rsp, r->ss);
	printf_serial("\tRAX=0x%llx RBX=0x%llx RCX=0x%llx RDX=0x%llx\r\n", r->rax, r->rbx, r->rcx, r->rdx);
	printf_serial("\tRSI=0x%llx RDI=0x%llx RBP=0x%llx\r\n", r->rsi, r->rdi, r->rbp);
	printf_serial("\tR8=0x%llx R9=0x%llx R10=0x%llx R11=0x%llx\r\n", r->r8, r->r9, r->r10, r->r11);
	printf_serial("\tR12=0x%llx R13=0x%llx R14=0x%llx R15=0x%llx\r\n", r->r12, r->r13, r->r14, r->r15);
}

// Called from idt.asm with interrupts off, on the stack the interrupt arrived on
WALLOS_USED void x86_interrupt_dispatch(struct interrupt_regs* regs) {
	interrupt_handler_t handler = __atomic_load_n(&int_handlers[regs->vector & 0xFF], __ATOMIC_ACQUIRE); // I hate these atomic funcs so much, I need to make my own interface for them
	if (handler) {
		handler(regs);
		return;
	}

	// Nothing claimed this vector
	printf("Unhandled interrupt. Vector %llu\n", regs->vector);
	printf_serial("[IDT] Unhandled interrupt (%llu)\r\n", regs->vector);
	dump_regs(regs);

	panic_s("Unhandled interrupt.");
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// First 32 (really it's 22) hardware exceptions.
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// A fault taken from ring 3 ends that task instead of halting the machine.
// task_fault() never returns. A fault in kernel mode is a kernel bug, so it panics.
// Exit reason is TASK_EXIT_FAULT, with status = vector | (error code << 8))
static void exception_fault(const struct interrupt_regs* r, const char* msg) {
	if (interrupt_from_user(r)) task_fault((int) ((r->vector & 0xFF) | ((r->error_code & 0xFFFF) << 8)));
	dump_regs(r);
	panic_s(msg);
}

#define EXCEPTION_FAULT(name, msg) \
	WALLOS_INTERRUPT_HANDLER void name(struct interrupt_regs* regs) { exception_fault(regs, msg); }
// Exceptions that aren't "the task did something wrong". These always panic.
#define EXCEPTION_PANIC(name, msg) \
	WALLOS_INTERRUPT_HANDLER void name(struct interrupt_regs* regs) { \
		dump_regs(regs); \
		panic_s(msg); \
	}

// These are all faults
EXCEPTION_FAULT(divide_by_zero_handler, "Divide By Zero Exception has occurred.")
EXCEPTION_FAULT(breakpoint_handler, "Breakpoint Exception has occurred.")
EXCEPTION_FAULT(overflow_handler, "Overflow Exception has occurred.")
EXCEPTION_FAULT(bound_range_exceeded_handler, "Bound Range Exceeded Exception has occurred.")
EXCEPTION_FAULT(invalid_opcode_handler, "Invalid Opcode Exception has occurred.")
EXCEPTION_FAULT(device_not_available_handler, "Device Not Available Exception has occurred.")
EXCEPTION_FAULT(invalid_tss_handler, "Invalid TSS Exception has occurred.")
EXCEPTION_FAULT(segment_not_present_handler, "Segment Not Present Exception has occurred.")
EXCEPTION_FAULT(stack_segment_fault_handler, "Stack-Segment Fault Exception has occurred.")
EXCEPTION_FAULT(x87_fpu_floating_point_error_handler, "x87 FPU Floating-Point Error Exception has occurred.")
EXCEPTION_FAULT(alignment_check_handler, "Alignment Check Exception has occurred.")
EXCEPTION_FAULT(simd_floating_point_exception_handler, "SIMD Floating-Point Exception has occurred.")

// These are all the panics
EXCEPTION_PANIC(debug_handler, "Debug Exception has occurred.") // this should probably be handled differently but I didnt feel like it
// The rest of these are mostly in the "hardware" category of exceptions, and I just don't want to deal with them (exception being #DF, I was just lazy with that)
EXCEPTION_PANIC(nmi_handler, "NMI (Non-Maskable Interrupt) has occurred.")
EXCEPTION_PANIC(double_fault_handler, "Double Fault Exception has occurred.")
EXCEPTION_PANIC(machine_check_handler, "Machine Check Exception has occurred.")
EXCEPTION_PANIC(virtualization_exception_handler, "Virtualization Exception has occurred.")
EXCEPTION_PANIC(control_protection_exception_handler, "Control Protection Exception has occurred.")

WALLOS_INTERRUPT_HANDLER void general_protection_fault_handler(struct interrupt_regs* regs) {
	if (interrupt_from_user(regs)) task_fault((int) ((regs->vector & 0xFF) | ((regs->error_code & 0xFFFF) << 8)));

	uword_t error_code = regs->error_code;
	uint16_t selector = error_code & 0xFFF8;
	uint8_t ti = (error_code >> 1) & 0x3;
	uint8_t ext = (error_code >> 2) & 0x1;
	uint16_t index = error_code >> 3;

	printf("\n=== GENERAL PROTECTION FAULT ===\n");
	printf("Error code: %llu\n", error_code);
	printf("  Selector: %u\n", selector);
	printf("  Selector index: %u\n", index);
	switch (ti) {
		case 0:  printf("  Table: GDT\n"); break;
		case 1:  printf("  Table: IDT\n"); break;
		case 2:  printf("  Table: LDT\n"); break;
		default: printf("  Table: IDT\n"); break;
	}
	printf("  External: %s\n", ext ? "yes" : "no");

	printf("\nCPU state:\n");
	printf("  RIP:    %p\n", (void*) regs->rip);
	printf("  CS:     0x%llx\n", regs->cs);
	printf("  RFLAGS: 0x%llx\n", regs->rflags);
	printf("  RSP:    %p\n", (void*) regs->rsp);
	printf("  SS:     0x%llx\n", regs->ss);

	// The faulting instruction. We only get here from ring 0, so RIP is kernel memory we can read.
	const uint8_t* ip = (const uint8_t*) regs->rip;
	printf("  Bytes:  %02x %02x %02x %02x %02x %02x %02x %02x\n", ip[0], ip[1], ip[2], ip[3], ip[4], ip[5], ip[6], ip[7]);

	uint64_t cr0, cr2, cr3, cr4;
	asm volatile("mov %%cr0, %0" : "=r"(cr0));
	asm volatile("mov %%cr2, %0" : "=r"(cr2));
	asm volatile("mov %%cr3, %0" : "=r"(cr3));
	asm volatile("mov %%cr4, %0" : "=r"(cr4));

	printf("\nControl registers:\n");
	printf("  CR0: 0x%llx\n", cr0);
	printf("  CR2: 0x%llx\n", cr2);
	printf("  CR3: 0x%llx\n", cr3);
	printf("  CR4: 0x%llx\n", cr4);

	dump_regs(regs);

	WALLOS_CLI();
	while (true) {
		WALLOS_HLT();
	}
}

WALLOS_INTERRUPT_HANDLER void page_fault_handler(struct interrupt_regs* regs) {
	// TODO: whenever I get to actually rewriting the VMM, demand paging goes here
	if (interrupt_from_user(regs)) task_fault((int) ((regs->vector & 0xFF) | ((regs->error_code & 0xFFFF) << 8)));

	uword_t error_code = regs->error_code;
	unsigned long cr2;
	asm volatile("movq %%cr2, %0" : "=r"(cr2));

	// unsigned long error_code;
	// asm volatile ("pop %0" : "=r" (error_code));
	char err[65];
	itoa(error_code, err, 2);
	int present = error_code & 0b1;
	error_code = error_code >> 1;
	int write = error_code & 0b1;
	error_code = error_code >> 1;
	int user_mode = error_code & 0b1;
	error_code = error_code >> 1;
	int reserved = error_code & 0b1;
	error_code = error_code >> 1;
	int instruction_fetch = error_code & 0b1;
	error_code = error_code >> 1;
	int protection_key = error_code & 0b1;
	error_code = error_code >> 1;
	int shadow_stack = error_code & 0b1;
	error_code = error_code >> 1;
	int sgx = error_code & 0b1;

	// Log the information
	logger(ERROR, "Page fault Error Code: %s\n", err);
	logger(ERROR, "Page fault at address (CR2): 0x%llx\n", cr2);
	logger(ERROR, "Present: %d, Write: %d, User Mode: %d, Reserved: %d, Instruction Fetch: %d, Protection: %d, Shadow Stack: %d, SGX: %d\n", present, write, user_mode, reserved, instruction_fetch, protection_key, shadow_stack, sgx);
	logger(ERROR, "Code that caused it: 0x%llx", regs->rip);

	printf_serial("Page fault Error Code: %s\r\n", err);
	printf_serial("Page fault at address (CR2): 0x%llx\r\n", cr2);
	printf_serial("Present: %d, Write: %d, User Mode: %d, Reserved: %d, Instruction Fetch: %d, Protection: %d, Shadow Stack: %d, SGX: %d\r\n", present, write, user_mode, reserved, instruction_fetch, protection_key, shadow_stack, sgx);
	printf_serial("Code that caused it: 0x%llx\r\n", regs->rip);
	dump_regs(regs);

	WALLOS_CLI();
	while (true) {
		WALLOS_HLT();
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// PIC stuff
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Add a spurious IRQ handler
WALLOS_INTERRUPT_HANDLER void spurious_irq7_handler(struct interrupt_regs* regs) {
	// Read the In-Service Register (ISR) from the master PIC
	outb(0x20, 0x0B);  // OCW3: Read ISR
	uint8_t isr = inb(0x20);

	// Check if IRQ 7 is actually in service (bit 7 set)
	if (isr & 0x80) {
		// Real IRQ 7 - send EOI
		outb(0x20, 0x20);
	}
	// If bit 7 is NOT set, this is spurious - do NOT send EOI
}

WALLOS_INTERRUPT_HANDLER void spurious_irq15_handler(struct interrupt_regs* regs) {
	// For slave spurious IRQ, always send EOI to master
	outb(0x20, 0x20);

	// Read ISR from slave PIC
	outb(0xA0, 0x0B);  // OCW3: Read ISR
	uint8_t isr = inb(0xA0);

	// Check if IRQ 15 is actually in service (bit 7 set)
	if (isr & 0x80) {
		// Real IRQ 15 - send EOI to slave
		outb(0xA0, 0x20);
	}
	// If spurious, don't send EOI to slave
}

// System interrupt 80
WALLOS_INTERRUPT_HANDLER void test_sys_handler(struct interrupt_regs* regs) {
	logger(WARN, "System Interrupt 80 Called.\n");
}

// Keyboard Handler.
WALLOS_INTERRUPT_HANDLER void keyboard_handler(struct interrupt_regs* regs) {
	handle_scancode(inb(0x60));
	// outb(0x20, 0x20);
	interrupt_eoi(1);
}

#include <system/timer.h>
#include <x86_64/timing.h>
WALLOS_INTERRUPT_HANDLER void system_pit(struct interrupt_regs* regs) {
	pit_handle_tick();
	// outb(0x20, 0x20);
	interrupt_eoi(0);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// IDT Entry Creation
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// The asm has one entry stub per vector.
// Every gate points at its stub, whether or not a handler is registered.
extern void* x86_isr_stub_table[256];

static void set_gate(uint8_t vector, uint64_t handler_addr, uint8_t ist, uint8_t type_attr) {
	struct idt_entry* entry = &idt[vector];
	entry->offset_low = (uint16_t) (handler_addr & 0xFFFF);
	entry->selector = 0x08; // Code segment selector
	entry->ist = ist;
	entry->type_attr = type_attr;
	entry->offset_mid = (uint16_t) ((handler_addr >> 16) & 0xFFFF);
	entry->offset_high = (uint32_t) ((handler_addr >> 32) & 0xFFFFFFFF);
	entry->zero = 0;
}

// We set the handler entry first before the gate so we don't accidentally get an interrupt before we get the entry set
static void set_idt_entry(uint8_t vector, interrupt_handler_t handler, uint8_t ist, uint8_t type_attr) {
	__atomic_store_n(&int_handlers[vector], handler, __ATOMIC_RELEASE);
	set_gate(vector, (uint64_t) (uintptr_t) x86_isr_stub_table[vector], ist, type_attr);
}

extern void idt_load(struct idt_descriptor* idt_desc);
extern void disablePIC();
extern void enablePS2();

/**
 * @brief Add an interrupt handler to the IDT.
 *
 * @param entry Entry number for the IDT. Corresponds to interrupt number (`int 80` calls idt entry 80)
 * @param handler Pointer to the interrupt handler.
 * @param ist Interrupt Stack Table (see chapter 8.9.4 in AMD Manual 2).
 * @param type_attr Type attributes. This includes P, DPL, and Gate type. (see https://wiki.osdev.org/IDT#Gate_Descriptor_2).
 * @return true If successfuly added.
 * @return false If trying to override hardware interrupts.
 */
bool add_interrupt_handler(uint8_t entry, interrupt_handler_t handler, uint8_t ist, uint8_t type_attr) {
	if (entry <= 32) return false;
	set_idt_entry(entry, handler, ist, type_attr);
	return true;
}

bool add_interrupt_handler_asm(uint8_t entry, void (*handler)(void), uint8_t ist, uint8_t type_attr) {
	if (entry <= 32) return false;
	__atomic_store_n(&int_handlers[entry], (interrupt_handler_t) NULL, __ATOMIC_RELEASE);
	set_gate(entry, (uint64_t) (uintptr_t) handler, ist, type_attr);
	return true;
}

void remove_interrupt_handler(uint8_t entry) {
	if (entry <= 32) return; // prevent myself from removing an important interrupt
	set_idt_entry(entry, NULL, 0, 0x8E);
}

void initIDT() {
	puts_vga_color("Enabling Interrupts.\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
	// Every vector goes to its stub. Anything without a handler ends up in the "unhandled" path.
	// We override the ones we care about later.
	for (int i = 0; i < 256; i++) {
		set_idt_entry((uint8_t) i, NULL, 0, 0x8E);
	}

	set_idt_entry(0, divide_by_zero_handler, 0, 0x8E);
	set_idt_entry(1, debug_handler, 0, 0x8E);
	set_idt_entry(2, nmi_handler, 0, 0x8E);
	set_idt_entry(3, breakpoint_handler, 0, 0x8E);
	set_idt_entry(4, overflow_handler, 0, 0x8E);
	set_idt_entry(5, bound_range_exceeded_handler, 0, 0x8E);
	set_idt_entry(6, invalid_opcode_handler, 0, 0x8E);
	set_idt_entry(7, device_not_available_handler, 0, 0x8E);
	set_idt_entry(8, double_fault_handler, 0, 0x8E);
	set_idt_entry(10, invalid_tss_handler, 0, 0x8E);
	set_idt_entry(11, segment_not_present_handler, 0, 0x8E);
	set_idt_entry(12, stack_segment_fault_handler, 0, 0x8E);
	set_idt_entry(13, general_protection_fault_handler, 0, 0x8E);
	set_idt_entry(14, page_fault_handler, 0, 0x8E);
	set_idt_entry(16, x87_fpu_floating_point_error_handler, 0, 0x8E);
	set_idt_entry(17, alignment_check_handler, 0, 0x8E);
	set_idt_entry(18, machine_check_handler, 0, 0x8E);
	set_idt_entry(19, simd_floating_point_exception_handler, 0, 0x8E);
	set_idt_entry(20, virtualization_exception_handler, 0, 0x8E);
	set_idt_entry(21, control_protection_exception_handler, 0, 0x8E);

	set_idt_entry(32, system_pit, 0, 0x8E);
	set_idt_entry(80, test_sys_handler, 0, 0x8E);

	// Deal with spurious PIC interrupts
	set_idt_entry(39, spurious_irq7_handler, 0, 0x8E);  // IRQ 7 spurious
	set_idt_entry(47, spurious_irq15_handler, 0, 0x8E); // IRQ 15 spurious

	// Set up the IDT descriptor
	idt_desc.limit = sizeof(idt) - 1;
	idt_desc.base = (uint64_t) &idt[0];

	// We need to disable the PIC
	disablePIC();
	// enableAPIC();
	// enablePS2();
	// Call the external assembly function to load the IDT
	idt_load(&idt_desc);
}

void ap_load_idt() {
	idt_load(&idt_desc);
}