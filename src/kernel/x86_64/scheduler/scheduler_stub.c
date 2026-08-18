#include <scheduler/scheduler.h>

// Defined in wallos_config.h


void arch_save_context(struct arch_context* ctx) {
	(void) ctx;
}
void arch_restore_context(struct arch_context* ctx) {
	(void) ctx;
	__builtin_unreachable();
}
void task_exit(int status) {
	(void) status;
	__builtin_unreachable();
}

// It's an asm stub
extern void arch_switch_context(struct arch_context* from, struct arch_context* to);
