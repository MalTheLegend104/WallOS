#include <drivers/serial.h>
#include <klibc/kprint.h>
#include <panic.h>
#include <stdbool.h>
#include <stdint.h>
#include <wallos_attributes.h>
#include <x86_64/ioapic.h>
#include <x86_64/lapic.h>

void acpi_poll_events(void);

extern volatile uint32_t ap_started_count;

const uint32_t NO_CPU = 0xFFFFFFFF;
const uint32_t ACK_SPIN_LIMIT = 50000000;

volatile uint32_t panic_cpu = NO_CPU;
volatile uint32_t cpus_halted = 0;

void halt_other_cpus() {
	uint32_t others = ap_started_count - 1;
	if (others == 0) {
		return;
	}

	printf_serial("Checked in CPUs (should be 1 'c' per CPU (%d total)): \n", others);
	lapic_broadcast_nmi_excluding_self();


	for (uint32_t i = 0; i < ACK_SPIN_LIMIT; i++) {
		if (__atomic_load_n(&cpus_halted, __ATOMIC_ACQUIRE) >= others) {
			printf_serial("\n");

			return;
		}
		__asm__ volatile("pause");
	}
	// Timed out, some CPU wont respond
	// Not our problem and the kernel is already unstable at this point
}

// Returns only on the CPU that won the panic election.
void begin_panic() {
	__asm__ volatile("cli");

	uint32_t me = lapic_current_id();
	uint32_t expected = NO_CPU;
	if (!__atomic_compare_exchange_n(&panic_cpu, &expected, me, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
		WALLOS_CLI_HLT(); // another CPU (or an earlier panic on this one) owns it
	}

	halt_other_cpus();
}

// Keep the panic CPU alive only to service ACPI shutdown/restart.
WALLOS_NORETURN void panic_idle() {
	lapic_timer_stop();
	ioapic_mask_all_except_sci();
	ioapic_route_sci_to(panic_cpu);

	for (;;) {
		__asm__ volatile("sti; hlt");
		__asm__ volatile("cli");
		acpi_poll_events();
	}
}

WALLOS_INTERRUPT_HANDLER bool panic_nmi_hook(void) {
	uint32_t owner = __atomic_load_n(&panic_cpu, __ATOMIC_ACQUIRE);

	// NMI was an actual NMI from something else.
	if (owner == NO_CPU) { return false; }

	// Not the owner, something else panicked, we need to respect it.
	if (owner != lapic_current_id()) {
		__atomic_fetch_add(&cpus_halted, 1, __ATOMIC_RELEASE);
		printf_serial("c");
		WALLOS_CLI_HLT();
	}

	// We're the owner, it was a stray NMI
	return true;
}

void panic() {
	panic_i(GENERAL_PURPOSE_PANIC);
}

#include <klibc/display.h>

void panic_s(const char* buf) {
	printf_serial("\n\n\nPANIC: %s\n", buf);

	begin_panic();
	display_panic(buf);
	panic_idle();
}

void panic_sa(const char** buf, uint8_t length) {
	begin_panic();
	display_panic_array(buf, length);
	panic_idle();
}

void panic_i(uint8_t panic) {
	switch (panic) {
		case GENERAL_PURPOSE_PANIC:
			panic_s("General Purpose Error.");
			break;
		default:
			panic_s("General Purpose Error.");
			break;
	}
}