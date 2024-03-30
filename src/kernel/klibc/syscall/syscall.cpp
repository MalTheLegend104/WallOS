#include <syscall/syscall.h>

#include <idt.h>

extern "C" {
	__attribute__((interrupt)) void handle_syscall(struct interrupt_frame* frame);
}

/**
 * @brief We expect the arguements to come across in the
 *
 */
void Syscall::initialize() {
	add_interrupt_handler(0x42, handle_syscall, 0, 0x8E);
}