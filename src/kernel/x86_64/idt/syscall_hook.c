#include <syscall/syscall.h>
#include <idt.h>
#include <stdio.h>

/* All of our actual interrupt handling is going to be dispatched through here. */

__attribute__((interrupt)) void handle_syscall(struct interrupt_frame* frame) {
	printf("Syscall 0x42 called.\n");
}