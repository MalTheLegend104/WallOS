#include <stdint.h>
#include <stdio.h>
#include <panic.h>
#include <klibc/logger.h>
// I aint touching the interrupt frame on 90% of these
#pragma GCC diagnostic ignored "-Wunused-parameter" 

#ifdef __x86_64__
typedef unsigned long long int uword_t;
#else
typedef unsigned int uword_t;
#endif


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

// I stole this from gcc
struct interrupt_frame {
	uword_t ip;
	uword_t cs;
	uword_t flags;
	uword_t sp;
	uword_t ss;
};
// __attribute__((interrupt)) forces gcc to mess with regiesters and use iret 
// it's required for interrupt handlers
__attribute__((interrupt)) void general_fault(struct interrupt_frame* frame) { panic_s("EA Sports, In the game."); }

// First 32(really it's 22) hardware exceptions.
// We will properly deal with these later.
__attribute__((interrupt)) void divide_by_zero_handler(struct interrupt_frame* frame) { panic_s("Divide By Zero Exception has occurred."); }
__attribute__((interrupt)) void debug_handler(struct interrupt_frame* frame) { panic_s("Debug Exception has occurred."); }
__attribute__((interrupt)) void nmi_handler(struct interrupt_frame* frame) { panic_s("NMI (Non-Maskable Interrupt) has occurred."); }
__attribute__((interrupt)) void breakpoint_handler(struct interrupt_frame* frame) { panic_s("Breakpoint Exception has occurred."); }
__attribute__((interrupt)) void overflow_handler(struct interrupt_frame* frame) { panic_s("Overflow Exception has occurred."); }
__attribute__((interrupt)) void bound_range_exceeded_handler(struct interrupt_frame* frame) { panic_s("Bound Range Exceeded Exception has occurred."); }
__attribute__((interrupt)) void invalid_opcode_handler(struct interrupt_frame* frame) {
	// // Obtain the opcode information
	// uint8_t* opcode_address = (uint8_t*) __builtin_return_address(0);
	// uint8_t invalid_opcode = *opcode_address;

	// // Perform any necessary actions or error handling specific to the invalid opcode exception

	// printf("Invalid Opcode Exception has occurred. Opcode: %d\n", invalid_opcode);
	// unsigned char opcode;
	// unsigned long long savedInstructionPointer;

	// // Retrieve the saved instruction pointer and opcode
	// __asm volatile(
	// "call 1f\n\t"  // Call forward to get the return address
	// 	"1: pop %0\n\t"  // Pop the return address into savedInstructionPointer
	// 	"movb (%%rip), %1"
	// 	: "=r" (savedInstructionPointer), "=q" (opcode)
	// 	:
	// 	: "memory"
	// 	);

	// printf("Caught #UD exception. Instruction Pointer: 0x%x, Opcode: 0x%x\n",
	// 	savedInstructionPointer, opcode);
	__asm volatile("hlt");
}
__attribute__((interrupt)) void device_not_available_handler(struct interrupt_frame* frame) { panic_s("Device Not Available Exception has occurred."); }

__attribute__((interrupt)) void double_fault_handler(struct interrupt_frame* frame) {
	panic_s("Double Fault Exception has occurred.");
	//return;
}

__attribute__((interrupt)) void invalid_tss_handler(struct interrupt_frame* frame) { panic_s("Invalid TSS Exception has occurred."); }
__attribute__((interrupt)) void segment_not_present_handler(struct interrupt_frame* frame) { panic_s("Segment Not Present Exception has occurred."); }
__attribute__((interrupt)) void stack_segment_fault_handler(struct interrupt_frame* frame) { panic_s("Stack-Segment Fault Exception has occurred."); }
__attribute__((interrupt)) void general_protection_fault_handler(struct interrupt_frame* frame) { panic_s("General Protection Fault Exception has occurred."); }
__attribute__((interrupt)) void page_fault_handler(struct interrupt_frame* frame) { panic_s("Page Fault has occurred."); }
__attribute__((interrupt)) void x87_fpu_floating_point_error_handler(struct interrupt_frame* frame) { panic_s("x87 FPU Floating-Point Error Exception has occurred."); }
__attribute__((interrupt)) void alignment_check_handler(struct interrupt_frame* frame) { panic_s("Alignment Check Exception has occurred."); }
__attribute__((interrupt)) void machine_check_handler(struct interrupt_frame* frame) { panic_s("Machine Check Exception has occurred."); }
__attribute__((interrupt)) void simd_floating_point_exception_handler(struct interrupt_frame* frame) { panic_s("SIMD Floating-Point Exception has occurred."); }
__attribute__((interrupt)) void virtualization_exception_handler(struct interrupt_frame* frame) { panic_s("Virtualization Exception has occurred."); }
__attribute__((interrupt)) void control_protection_exception_handler(struct interrupt_frame* frame) { panic_s("Control Protection Exception has occurred."); }

void set_idt_entry(struct idt_entry* entry, void (*handler)(), uint8_t ist, uint8_t type_attr) {
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

__attribute__((interrupt)) void test_sys_handler(struct interrupt_frame* frame) {
	logger(WARN, "System Interrupt 80 Called.");
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
	for (int i = 22; i < 32; i++) {
		set_idt_entry(&idt[i], general_fault, 0, 0x8E);
	}

	set_idt_entry(&idt[80], test_sys_handler, 0, 0x8E);

	// Set up the IDT descriptor
	idt_desc.limit = sizeof(idt) - 1;
	idt_desc.base = (uint64_t) &idt[0];

	// Call the external assembly function to load the IDT
	idt_load(&idt_desc);
}