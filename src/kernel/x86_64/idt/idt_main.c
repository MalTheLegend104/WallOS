#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <panic.h>
#include <klibc/logger.h>
#include <system/idt.h>
#include <stdbool.h>
#include <drivers/keyboard.h>
#include <drivers/serial.h>
#include <cpu_io.h>

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
// Generic IRQ Handlers for all interrupts
// This, at the very least, lets us determine if a random interrupt is called and unhandled
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#define DEFINE_IRQ_HANDLER(num) \
    __attribute__((interrupt)) void irq##num(struct interrupt_frame* frame) { \
        printf("irq %d called\n", num); \
        asm volatile("cli"); \
        asm volatile("hlt"); \
    }

// Generate handlers in groups of 10
#define EXPAND_IRQS_X0_X9(base) \
    DEFINE_IRQ_HANDLER(base##0) \
    DEFINE_IRQ_HANDLER(base##1) \
    DEFINE_IRQ_HANDLER(base##2) \
    DEFINE_IRQ_HANDLER(base##3) \
    DEFINE_IRQ_HANDLER(base##4) \
    DEFINE_IRQ_HANDLER(base##5) \
    DEFINE_IRQ_HANDLER(base##6) \
    DEFINE_IRQ_HANDLER(base##7) \
    DEFINE_IRQ_HANDLER(base##8) \
    DEFINE_IRQ_HANDLER(base##9)

// Generate all 256 interrupt handlers (0-255)
EXPAND_IRQS_X0_X9()       // 0-9
EXPAND_IRQS_X0_X9(1)      // 10-19
EXPAND_IRQS_X0_X9(2)      // 20-29
EXPAND_IRQS_X0_X9(3)      // 30-39
EXPAND_IRQS_X0_X9(4)      // 40-49
EXPAND_IRQS_X0_X9(5)      // 50-59
EXPAND_IRQS_X0_X9(6)      // 60-69
EXPAND_IRQS_X0_X9(7)      // 70-79
EXPAND_IRQS_X0_X9(8)      // 80-89
EXPAND_IRQS_X0_X9(9)      // 90-99
EXPAND_IRQS_X0_X9(10)     // 100-109
EXPAND_IRQS_X0_X9(11)     // 110-119
EXPAND_IRQS_X0_X9(12)     // 120-129
EXPAND_IRQS_X0_X9(13)     // 130-139
EXPAND_IRQS_X0_X9(14)     // 140-149
EXPAND_IRQS_X0_X9(15)     // 150-159
EXPAND_IRQS_X0_X9(16)     // 160-169
EXPAND_IRQS_X0_X9(17)     // 170-179
EXPAND_IRQS_X0_X9(18)     // 180-189
EXPAND_IRQS_X0_X9(19)     // 190-199
EXPAND_IRQS_X0_X9(20)     // 200-209
EXPAND_IRQS_X0_X9(21)     // 210-219
EXPAND_IRQS_X0_X9(22)     // 220-229
EXPAND_IRQS_X0_X9(23)     // 230-239
EXPAND_IRQS_X0_X9(24)     // 240-249
// We manually do these so we don't end up with 4 extra handlers.
DEFINE_IRQ_HANDLER(250)
DEFINE_IRQ_HANDLER(251)
DEFINE_IRQ_HANDLER(252)
DEFINE_IRQ_HANDLER(253)
DEFINE_IRQ_HANDLER(254)
DEFINE_IRQ_HANDLER(255)

// Generates all the stub handlers
void (*isr_stub_table[256])(struct interrupt_frame*) = {
	irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7, irq8, irq9,
	irq10, irq11, irq12, irq13, irq14, irq15, irq16, irq17, irq18, irq19,
	irq20, irq21, irq22, irq23, irq24, irq25, irq26, irq27, irq28, irq29,
	irq30, irq31, irq32, irq33, irq34, irq35, irq36, irq37, irq38, irq39,
	irq40, irq41, irq42, irq43, irq44, irq45, irq46, irq47, irq48, irq49,
	irq50, irq51, irq52, irq53, irq54, irq55, irq56, irq57, irq58, irq59,
	irq60, irq61, irq62, irq63, irq64, irq65, irq66, irq67, irq68, irq69,
	irq70, irq71, irq72, irq73, irq74, irq75, irq76, irq77, irq78, irq79,
	irq80, irq81, irq82, irq83, irq84, irq85, irq86, irq87, irq88, irq89,
	irq90, irq91, irq92, irq93, irq94, irq95, irq96, irq97, irq98, irq99,
	irq100, irq101, irq102, irq103, irq104, irq105, irq106, irq107, irq108, irq109,
	irq110, irq111, irq112, irq113, irq114, irq115, irq116, irq117, irq118, irq119,
	irq120, irq121, irq122, irq123, irq124, irq125, irq126, irq127, irq128, irq129,
	irq130, irq131, irq132, irq133, irq134, irq135, irq136, irq137, irq138, irq139,
	irq140, irq141, irq142, irq143, irq144, irq145, irq146, irq147, irq148, irq149,
	irq150, irq151, irq152, irq153, irq154, irq155, irq156, irq157, irq158, irq159,
	irq160, irq161, irq162, irq163, irq164, irq165, irq166, irq167, irq168, irq169,
	irq170, irq171, irq172, irq173, irq174, irq175, irq176, irq177, irq178, irq179,
	irq180, irq181, irq182, irq183, irq184, irq185, irq186, irq187, irq188, irq189,
	irq190, irq191, irq192, irq193, irq194, irq195, irq196, irq197, irq198, irq199,
	irq200, irq201, irq202, irq203, irq204, irq205, irq206, irq207, irq208, irq209,
	irq210, irq211, irq212, irq213, irq214, irq215, irq216, irq217, irq218, irq219,
	irq220, irq221, irq222, irq223, irq224, irq225, irq226, irq227, irq228, irq229,
	irq230, irq231, irq232, irq233, irq234, irq235, irq236, irq237, irq238, irq239,
	irq240, irq241, irq242, irq243, irq244, irq245, irq246, irq247, irq248, irq249,
	irq250, irq251, irq252, irq253, irq254, irq255
};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// First 32(really it's 22) hardware exceptions.
// We will properly deal with these later.
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
__attribute__((interrupt)) void divide_by_zero_handler(struct interrupt_frame* frame) { panic_s("Divide By Zero Exception has occurred."); }
__attribute__((interrupt)) void debug_handler(struct interrupt_frame* frame) { panic_s("Debug Exception has occurred."); }
__attribute__((interrupt)) void nmi_handler(struct interrupt_frame* frame) { panic_s("NMI (Non-Maskable Interrupt) has occurred."); }
__attribute__((interrupt)) void breakpoint_handler(struct interrupt_frame* frame) { panic_s("Breakpoint Exception has occurred."); }
__attribute__((interrupt)) void overflow_handler(struct interrupt_frame* frame) { panic_s("Overflow Exception has occurred."); }
__attribute__((interrupt)) void bound_range_exceeded_handler(struct interrupt_frame* frame) { panic_s("Bound Range Exceeded Exception has occurred."); }
__attribute__((interrupt)) void invalid_opcode_handler(struct interrupt_frame* frame) { panic_s("Invalid Opcode Exception has occurred."); }
__attribute__((interrupt)) void device_not_available_handler(struct interrupt_frame* frame) { panic_s("Device Not Available Exception has occurred."); }
__attribute__((interrupt)) void double_fault_handler(struct interrupt_frame* frame, uword_t error_code) { panic_s("Double Fault Exception has occurred."); }
__attribute__((interrupt)) void invalid_tss_handler(struct interrupt_frame* frame, uword_t error_code) { panic_s("Invalid TSS Exception has occurred."); }
__attribute__((interrupt)) void segment_not_present_handler(struct interrupt_frame* frame, uword_t error_code) { panic_s("Segment Not Present Exception has occurred."); }
__attribute__((interrupt)) void stack_segment_fault_handler(struct interrupt_frame* frame, uword_t error_code) { panic_s("Stack-Segment Fault Exception has occurred."); }
__attribute__((interrupt)) void general_protection_fault_handler(struct interrupt_frame* frame, uword_t error_code) {
	uint16_t selector = error_code & 0xFFF8;
	uint8_t ti = (error_code >> 1) & 0x3;
	uint8_t ext = (error_code >> 2) & 0x1;
	uint16_t index = error_code >> 3;

	printf("\n=== GENERAL PROTECTION FAULT ===\n");
	printf("Error code: %llu\n", error_code);
	printf("  Selector index: %u\n", index);
	printf("  Table: %s\n",
		ti == 0 ? "GDT" :
		ti == 1 ? "IDT" :
		ti == 2 ? "LDT" : "IDT");
	printf("  External: %s\n", ext ? "yes" : "no");

	printf("\nCPU state:\n");
	printf("  RIP:    %p\n", (void*) frame->ip);
	printf("  CS:     0x%llx\n", frame->cs);
	printf("  RFLAGS: 0x%llx\n", frame->flags);
	printf("  RSP:    %p\n", (void*) frame->sp);
	printf("  SS:     0x%llx\n", frame->ss);

	uint64_t cr0, cr2, cr3, cr4;
	asm volatile ("mov %%cr0, %0" : "=r"(cr0));
	asm volatile ("mov %%cr2, %0" : "=r"(cr2));
	asm volatile ("mov %%cr3, %0" : "=r"(cr3));
	asm volatile ("mov %%cr4, %0" : "=r"(cr4));

	printf("\nControl registers:\n");
	printf("  CR0: 0x%llx\n", cr0);
	printf("  CR2: 0x%llx\n", cr2);
	printf("  CR3: 0x%llx\n", cr3);
	printf("  CR4: 0x%llx\n", cr4);

	asm volatile ("cli");
	for (;;)
		asm volatile ("hlt");
}
__attribute__((interrupt)) void x87_fpu_floating_point_error_handler(struct interrupt_frame* frame) { panic_s("x87 FPU Floating-Point Error Exception has occurred."); }
__attribute__((interrupt)) void alignment_check_handler(struct interrupt_frame* frame, uword_t error_code) { panic_s("Alignment Check Exception has occurred."); }
__attribute__((interrupt)) void machine_check_handler(struct interrupt_frame* frame) { panic_s("Machine Check Exception has occurred."); }
__attribute__((interrupt)) void simd_floating_point_exception_handler(struct interrupt_frame* frame) { panic_s("SIMD Floating-Point Exception has occurred."); }
__attribute__((interrupt)) void virtualization_exception_handler(struct interrupt_frame* frame) { panic_s("Virtualization Exception has occurred."); }
__attribute__((interrupt)) void control_protection_exception_handler(struct interrupt_frame* frame, uword_t error_code) { panic_s("Control Protection Exception has occurred."); }

__attribute__((interrupt)) void page_fault_handler(struct interrupt_frame* frame, uword_t error_code) {
	unsigned long cr2;
	asm volatile ("movq %%cr2, %0" : "=r" (cr2));

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
	logger(ERROR, "Present: %d, Write: %d, User Mode: %d, Reserved: %d, Instruction Fetch: %d, Protection: %d, Shadow Stack: %d, SGX: %d\n",
		present, write, user_mode, reserved, instruction_fetch, protection_key, shadow_stack, sgx);
	logger(ERROR, "Code that caused it: 0x%llx", frame->ip);

	printf_serial("Page fault Error Code: %s\r\n", err);
	printf_serial("Page fault at address (CR2): 0x%llx\r\n", cr2);
	printf_serial("Present: %d, Write: %d, User Mode: %d, Reserved: %d, Instruction Fetch: %d, Protection: %d, Shadow Stack: %d, SGX: %d\r\n",
		present, write, user_mode, reserved, instruction_fetch, protection_key, shadow_stack, sgx);
	printf_serial("Code that caused it: 0x%llx", frame->ip);
	asm volatile("hlt");
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// PIC stuff
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Add a spurious IRQ handler
__attribute__((interrupt)) void spurious_irq7_handler(struct interrupt_frame* frame) {
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

__attribute__((interrupt)) void spurious_irq15_handler(struct interrupt_frame* frame) {
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
__attribute__((interrupt)) void test_sys_handler(struct interrupt_frame* frame) {
	logger(WARN, "System Interrupt 80 Called.\n");
}

// Keyboard Handler.
__attribute__((interrupt)) void keyboard_handler(struct interrupt_frame* frame) {
	handle_scancode(inb(0x60));
	outb(0x20, 0x20);
}

#include <system/timing.h>
__attribute__((interrupt)) void system_pit(struct interrupt_frame* frame) {
	incriment_sys_time();
	outb(0x20, 0x20);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// IDT Entry Creation
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
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

void set_idt_entry_err(struct idt_entry* entry, void (*handler)(struct interrupt_frame*, uword_t), uint8_t ist, uint8_t type_attr) {
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

bool add_interrupt_handler_asm(uint8_t entry, void(*handler)(), uint8_t ist, uint8_t type_attr) {
	if (entry <= 32) return false;
	set_idt_entry(&idt[entry], handler, ist, type_attr);
	return true;
}

void initIDT() {
	puts_vga_color("Enabling Interrupts.\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
	// Set generic handlers for all interrupts
	// We override the ones we care about later.
	for (int i = 0; i < 256; i++) {
		set_idt_entry(&idt[i], isr_stub_table[i], 0, 0x8E);
	}

	set_idt_entry(&idt[0], divide_by_zero_handler, 0, 0x8E);
	set_idt_entry(&idt[1], debug_handler, 0, 0x8E);
	set_idt_entry(&idt[2], nmi_handler, 0, 0x8E);
	set_idt_entry(&idt[3], breakpoint_handler, 0, 0x8E);
	set_idt_entry(&idt[4], overflow_handler, 0, 0x8E);
	set_idt_entry(&idt[5], bound_range_exceeded_handler, 0, 0x8E);
	set_idt_entry(&idt[6], invalid_opcode_handler, 0, 0x8E);
	set_idt_entry(&idt[7], device_not_available_handler, 0, 0x8E);
	set_idt_entry_err(&idt[8], double_fault_handler, 0, 0x8E);
	set_idt_entry_err(&idt[10], invalid_tss_handler, 0, 0x8E);
	set_idt_entry_err(&idt[11], segment_not_present_handler, 0, 0x8E);
	set_idt_entry_err(&idt[12], stack_segment_fault_handler, 0, 0x8E);
	set_idt_entry_err(&idt[13], general_protection_fault_handler, 0, 0x8E);
	set_idt_entry_err(&idt[14], page_fault_handler, 0, 0x8E);
	set_idt_entry(&idt[16], x87_fpu_floating_point_error_handler, 0, 0x8E);
	set_idt_entry_err(&idt[17], alignment_check_handler, 0, 0x8E);
	set_idt_entry(&idt[18], machine_check_handler, 0, 0x8E);
	set_idt_entry(&idt[19], simd_floating_point_exception_handler, 0, 0x8E);
	set_idt_entry(&idt[20], virtualization_exception_handler, 0, 0x8E);
	set_idt_entry_err(&idt[21], control_protection_exception_handler, 0, 0x8E);

	set_idt_entry(&idt[0x20], system_pit, 0, 0x8E);
	set_idt_entry(&idt[80], test_sys_handler, 0, 0x8E);

	// Deal with spurious PIC interrupts
	set_idt_entry(&idt[39], spurious_irq7_handler, 0, 0x8E);   // IRQ 7 spurious
	set_idt_entry(&idt[47], spurious_irq15_handler, 0, 0x8E);  // IRQ 15 spurious

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