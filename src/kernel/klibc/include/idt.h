#ifndef IDT_H
#define IDT_H
#include <stdint.h>
#include <stdbool.h>

#ifdef __x86_64__
typedef unsigned long long int uword_t;
#else
typedef unsigned int uword_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

	// I stole this from gcc
	struct interrupt_frame {
		uword_t ip;
		uword_t cs;
		uword_t flags;
		uword_t sp;
		uword_t ss;
	};

	static inline void pushregs() {
		__asm volatile(
		"push %%rax"
			"push   %%rbp"
			"mov    %%rbp,%%rsp"
			"push   %%r11"
			"push   %%r10"
			"push   %%r9"
			"push   %%r8"
			"push   %%rdi"
			"push   %%rsi"
			"push   %%rcx"
			"push   %%rdx"
			"push   %%rax"
			:
		:
			: "memory" // Indicate that memory is being modified
			);
	}

	static inline void popregs() {

	}

	bool add_interrupt_handler(uint8_t entry, void (*handler)(struct interrupt_frame*), uint8_t ist, uint8_t type_attr);

	void setup_idt();
	/**
	 * @brief Enable the IRQ number on the legacy 8529 PIC.
	 * Ideally we should use the APIC, but legacy PIC support is baked in so idrc.
	 *
	 * @param irq IRQ number to enable on the PIC.
	 */
	extern void irq_enable(uint8_t irq);

	/**
	 * @brief Disable the IRQ number on the legacy 8529 PIC.
	 * Ideally we should use the APIC, but legacy PIC support is baked in so idrc.
	 *
	 * @param irq IRQ number to enable on the PIC.
	 */
	extern void irq_disable(uint8_t irq);
#ifdef __cplusplus
	}
#endif

#endif // IDT_H