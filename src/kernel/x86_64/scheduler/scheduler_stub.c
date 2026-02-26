#include <scheduler/scheduler.h>

// Defined in wallos_config.h
cpu_t cpus[WALLOS_SYSTEM_MAX_CPU];

void arch_init_cpus() {
	// We need to do everything here to "discover" CPUs (we can use ACPI tables)
	// Init APIC timers.

}

void arch_save_context(struct arch_context* ctx) {

}
void arch_restore_context(struct arch_context* ctx) {

}
void task_exit(int status) {

}

// It's an asm stub
extern void arch_switch_context(struct arch_context* from, struct arch_context* to);
