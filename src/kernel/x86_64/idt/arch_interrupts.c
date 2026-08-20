#include <arch.h>
#include <stdint.h>

// This probably needs a bit more thought in how to do this with multitasking

static uint16_t disable_count = 0;

void cpu_disable_interrupts() {
	disable_count++;
	__asm__ volatile("cli" ::: "memory");
}

void cpu_enable_interrupts() {
	disable_count--;
	if (disable_count == 0) __asm__ volatile("sti" ::: "memory");
}