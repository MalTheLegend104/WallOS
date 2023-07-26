#include <idt.h>
#include <stdio.h>
#include <klibc/kprint.h>

// IDT entry for the handler.
extern __attribute__((interrupt)) void system_pit(struct interrupt_frame* frame);

size_t system_execution_time;

void sleep(size_t ms) {
	size_t start = system_execution_time;
	// Busy waiting probably isn't the best way to do this but oh well.
	while ((system_execution_time - start) < ms) {}
	return;
}

size_t get_system_up_time() {
	return system_execution_time;
}

// TODO, this should be an "internal" function, only accessable to the kernel.
// Most of this file should be. sleep() should be a syscall that only returns when done.
void incriment_sys_time() {
	//printf("e");
	system_execution_time += 1;
}

// Function to initialize the PIT and set up the desired interrupt frequency
void pit_init(uint16_t frequency_ms) {
	system_execution_time = 0;

	set_colors(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
	printf("Install Timer at %dHz\n", frequency_ms);
	set_to_last();

	// Channel 0 Mode 2 for the most accuracy.
	// It aint perfect but it's close enough
	int divisor = 1193180 / frequency_ms;

	//the top and bottom bytes of the divisor must be sent separately
	char low = (char) (divisor & 0xFF);
	char high = (char) ((divisor >> 8) & 0xFF);

	//set PIT channel 0 to use the new divisor
	outb(0x43, 0x36);
	outb(0x40, low);
	outb(0x40, high);

	// Dont ask me what this is doing, it just works
	__asm__ volatile("cli");
	outb(0x21, 0xFD);
	irq_enable(0);
	__asm__ volatile("sti");
}
