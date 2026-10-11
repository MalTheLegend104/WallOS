#ifndef IDT_H
#define IDT_H
#include <stdbool.h>
#include <stdint.h>

#ifdef __x86_64__
typedef unsigned long long int uword_t;
#else
typedef unsigned int uword_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * This is everything that an interrupt handler gets.
	 * The handler can read AND change any of it.
	 * The interrupted code resumes with whatever is in here when the handler returns.
	 */
	struct interrupt_regs {
		uword_t r15, r14, r13, r12, r11, r10, r9, r8;
		uword_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
		uword_t vector;
		uword_t error_code;
		uword_t rip, cs, rflags, rsp, ss; // pushed by the CPU
	};

	/**
	 * Checks if the interrupt was taken from ring 3.
	 */
	static inline bool interrupt_from_user(const struct interrupt_regs* regs) { return (regs->cs & 3) != 0; }

	/**
	 * An interrupt/exception handler.
	 * It runs on whatever kernel stack the interrupt arrived on, with interrupts off (interrupt gate), and returns normally.
	 * Mark it WALLOS_INTERRUPT_HANDLER to keep it away from the SSE registers, ideally any calls to other functions should also use the same attribute.
	 * EOI must be called by the handler if required.
	 */
	typedef void (*interrupt_handler_t)(struct interrupt_regs* regs);

	/**
	 * @brief Install a C handler for a vector.
	 *
	 * @param entry Vector (<= 32)
	 * @param handler See interrupt_handler_t.
	 * @param ist Interrupt Stack Table index (0 = none).
	 * @param type_attr Gate attributes (P, DPL, type). 0x8E = present, ring 0, interrupt gate.
	 */
	bool add_interrupt_handler(uint8_t entry, interrupt_handler_t handler, uint8_t ist, uint8_t type_attr);

	/**
	 * @brief Install a raw asm entry point.
	 * It is NOT wrapped, it must do its own swapgs (if it can be entered from ring 3), save the registers it uses and iretq.
	 */
	bool add_interrupt_handler_asm(uint8_t entry, void (*handler)(void), uint8_t ist, uint8_t type_attr);
	void remove_interrupt_handler(uint8_t entry);

	void initIDT();

	/**
	 * @brief Meant to be called during AP bringup.
	 * Loads the same IDT that we do on the BSP.
	 */
	void ap_load_idt();
	/**
	 * @brief Enable the IRQ number on the legacy 8529 PIC.
	 * Automatically handles PIC/IOAPIC routing.
	 *
	 * @param irq IRQ number to enable on the PIC.
	 */
	void irq_enable(uint8_t irq);

	/**
	 * @brief Disable the IRQ number on the legacy 8529 PIC.
	 * Automatically handles PIC/IOAPIC routing.
	 *
	 * @param irq IRQ number to enable on the PIC.
	 */
	void irq_disable(uint8_t irq);

	bool irq_is_enabled(uint8_t irq);
	void irq_route_enabled_to_ioapic(uint32_t dest_apic_id);

	void irq_set_level_triggered(uint8_t irq);
	void irq_set_edge_triggered(uint8_t irq);

	/**
	 * @brief Abstracted EOI handler.
	 * It will send EOI to LAPIC if enabled, regular PIC otherwise.
	 *
	 * @param irq_number IRQ number we are servicing (0-15).
	 */
	void interrupt_eoi(uint8_t irq_number);
#ifdef __cplusplus
}
#endif

#endif // IDT_H