#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <panic.h>
#include <klibc/logger.h>
#include <idt.h>
#include <stdbool.h>
#include <drivers/keyboard.h>
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

// __attribute__((interrupt)) forces gcc to mess with regiesters and use iret 
// it's required for interrupt handlers
__attribute__((interrupt)) void general_fault(struct interrupt_frame* frame) {
	// /uint16_t cs = frame->cs;
	//uint8_t interrupt_number = cs & 0xFF; // Extract the lower 8 bits
	//uint8_t interrupt_number;
	//asm volatile ("movb $0, %0; int $$0; movb %%al, %0" : "=r" (interrupt_number));

	//printf("Interrupt %d called.", interrupt_number);
	panic_s("EA Sports, In the game.");
	//__asm__ volatile("hlt");
}

// First 32(really it's 22) hardware exceptions.
// We will properly deal with these later.
__attribute__((interrupt)) void divide_by_zero_handler(struct interrupt_frame* frame) { panic_s("Divide By Zero Exception has occurred."); }
__attribute__((interrupt)) void debug_handler(struct interrupt_frame* frame) { panic_s("Debug Exception has occurred."); }
__attribute__((interrupt)) void nmi_handler(struct interrupt_frame* frame) { panic_s("NMI (Non-Maskable Interrupt) has occurred."); }
__attribute__((interrupt)) void breakpoint_handler(struct interrupt_frame* frame) { panic_s("Breakpoint Exception has occurred."); }
__attribute__((interrupt)) void overflow_handler(struct interrupt_frame* frame) { panic_s("Overflow Exception has occurred."); }
__attribute__((interrupt)) void bound_range_exceeded_handler(struct interrupt_frame* frame) { panic_s("Bound Range Exceeded Exception has occurred."); }
__attribute__((interrupt)) void invalid_opcode_handler(struct interrupt_frame* frame) { panic_s("Invalid Opcode Exception has occurred."); }
__attribute__((interrupt)) void device_not_available_handler(struct interrupt_frame* frame) { panic_s("Device Not Available Exception has occurred."); }
__attribute__((interrupt)) void double_fault_handler(struct interrupt_frame* frame) { panic_s("Double Fault Exception has occurred."); }
__attribute__((interrupt)) void invalid_tss_handler(struct interrupt_frame* frame) { panic_s("Invalid TSS Exception has occurred."); }
__attribute__((interrupt)) void segment_not_present_handler(struct interrupt_frame* frame) { panic_s("Segment Not Present Exception has occurred."); }
__attribute__((interrupt)) void stack_segment_fault_handler(struct interrupt_frame* frame) { panic_s("Stack-Segment Fault Exception has occurred."); }
__attribute__((interrupt)) void general_protection_fault_handler(struct interrupt_frame* frame) { panic_s("General Protection Fault Exception has occurred."); }
__attribute__((interrupt)) void x87_fpu_floating_point_error_handler(struct interrupt_frame* frame) { panic_s("x87 FPU Floating-Point Error Exception has occurred."); }
__attribute__((interrupt)) void alignment_check_handler(struct interrupt_frame* frame) { panic_s("Alignment Check Exception has occurred."); }
__attribute__((interrupt)) void machine_check_handler(struct interrupt_frame* frame) { panic_s("Machine Check Exception has occurred."); }
__attribute__((interrupt)) void simd_floating_point_exception_handler(struct interrupt_frame* frame) { panic_s("SIMD Floating-Point Exception has occurred."); }
__attribute__((interrupt)) void virtualization_exception_handler(struct interrupt_frame* frame) { panic_s("Virtualization Exception has occurred."); }
__attribute__((interrupt)) void control_protection_exception_handler(struct interrupt_frame* frame) { panic_s("Control Protection Exception has occurred."); }

__attribute__((interrupt)) void page_fault_handler(struct interrupt_frame* frame) {
	unsigned long cr2;
	asm volatile ("movq %%cr2, %0" : "=r" (cr2));

	unsigned long error_code;
	asm volatile ("pop %0" : "=r" (error_code));
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
	logger(ERROR, "Page fault at address (CR2): %llu\n", cr2);
	logger(ERROR, "Present: %d, Write: %d, User Mode: %d, Reserved: %d, Instruction Fetch: %d, Protection: %d, Shadow Stack: %d, SGX: %d\n",
		present, write, user_mode, reserved, instruction_fetch, protection_key, shadow_stack, sgx);
	asm volatile("hlt");
}
// System interrupt 80
__attribute__((interrupt)) void test_sys_handler(struct interrupt_frame* frame) {
	logger(WARN, "System Interrupt 80 Called.\n");
}

// Keyboard Handler.
__attribute__((interrupt)) void keyboard_handler(struct interrupt_frame* frame) {
	handle_scancode(inb(0x60));
	outb(0x20, 0x20);
}

void set_idt_entry(struct idt_entry* entry, void (*handler)(struct interrupt_frame*), uint8_t ist, uint8_t type_attr) {
	uint64_t handler_addr = (uint64_t) handler;
	entry->offset_low = (uint16_t) (handler_addr & 0xFFFF);
	entry->selector = 0x08; // Code segment selector
	entry->ist = ist;
	entry->type_attr = type_attr;
	entry->offset_mid = (uint16_t) ((handler_addr >> 16) & 0xFFFF);
	entry->offset_high = (uint32_t) ((handler_addr >> 32) & 0xFFFFFFFF);
	entry->zero = 0;
}

extern void idt_load(struct idt_descriptor* idt_desc);
extern void disablePIC();
extern void enableAPIC();
extern void enablePS2();
extern void reEnableIRQ1();

#include <timing.h>
__attribute__((interrupt)) void system_pit(struct interrupt_frame* frame) {
	incriment_sys_time();
	outb(0x20, 0x20);
}

/**
 * @brief Add an interrupt handler to the IDT. You *must* compile the handler with "-mgeneral-regs-only".
 * Use __attribute__((interrupt)) and __attribute__ ((__target__ ("general-regs-only"))) on the function to ensure proper compilation.
 *
 * For proper format for interrupt & exception handlers, see:
 * https://gcc.gnu.org/onlinedocs/gcc/x86-Function-Attributes.html#index-interrupt-function-attribute_002c-x86
 *
 * @param entry Entry number for the IDT. Corresponds to interrupt number (`int 80` calls idt entry 80)
 * @param handler Pointer to the interrupt handler.
 * @param ist Interrupt Stack Table (see chapter 8.9.4 in AMD Manual 2).
 * @param type_attr Type attributes. This includes P, DPL, and Gate type. (see https://wiki.osdev.org/IDT#Gate_Descriptor_2).
 * @return true If successfuly added.
 * @return false If trying to override hardware interrupts.
 */
bool add_interrupt_handler(uint8_t entry, void (*handler)(struct interrupt_frame*), uint8_t ist, uint8_t type_attr) {
	if (entry <= 32) return false;
	set_idt_entry(&idt[entry], handler, ist, type_attr);
	return true;
}

void setup_idt() {
	// Create IDT entries for the first 32 interrupts

	set_idt_entry(&idt[0], divide_by_zero_handler, 0, 0x8E);
	set_idt_entry(&idt[1], debug_handler, 0, 0x8E);
	set_idt_entry(&idt[2], nmi_handler, 0, 0x8E);
	set_idt_entry(&idt[3], breakpoint_handler, 0, 0x8E);
	set_idt_entry(&idt[4], overflow_handler, 0, 0x8E);
	set_idt_entry(&idt[5], bound_range_exceeded_handler, 0, 0x8E);
	set_idt_entry(&idt[6], invalid_opcode_handler, 0, 0x8E);
	set_idt_entry(&idt[7], device_not_available_handler, 0, 0x8E);
	set_idt_entry(&idt[8], double_fault_handler, 0, 0x8E);
	set_idt_entry(&idt[10], invalid_tss_handler, 0, 0x8E);
	set_idt_entry(&idt[11], segment_not_present_handler, 0, 0x8E);
	set_idt_entry(&idt[12], stack_segment_fault_handler, 0, 0x8E);
	set_idt_entry(&idt[13], general_protection_fault_handler, 0, 0x8E);
	set_idt_entry(&idt[14], page_fault_handler, 0, 0x8E);
	set_idt_entry(&idt[16], x87_fpu_floating_point_error_handler, 0, 0x8E);
	set_idt_entry(&idt[17], alignment_check_handler, 0, 0x8E);
	set_idt_entry(&idt[18], machine_check_handler, 0, 0x8E);
	set_idt_entry(&idt[19], simd_floating_point_exception_handler, 0, 0x8E);
	set_idt_entry(&idt[20], virtualization_exception_handler, 0, 0x8E);
	set_idt_entry(&idt[21], control_protection_exception_handler, 0, 0x8E);

	// ... Set IDT entries for other interrupts ...
	for (int i = 22; i < 256; i++) {
		set_idt_entry(&idt[i], general_fault, 0, 0x8E);
	}

	set_idt_entry(&idt[0x20], system_pit, 0, 0x8E);
	set_idt_entry(&idt[80], test_sys_handler, 0, 0x8E);

	// Set up the IDT descriptor
	idt_desc.limit = sizeof(idt) - 1;
	idt_desc.base = (uint64_t) &idt[0];

	// We need to disable the PIC
	disablePIC();
	//enableAPIC();
	//enablePS2();
	// Call the external assembly function to load the IDT
	idt_load(&idt_desc);
}