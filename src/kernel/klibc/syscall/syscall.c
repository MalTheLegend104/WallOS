#include <syscall/syscall.h>
#include <idt.h>

/* We push the arguments in a specific order
 * The first few are pushed into normal registers, the rest are in the extended registers
 * rax -> Syscall Number
 * rdi -> Arg1
 * rsi -> Arg2
 * rdx -> Arg3
 * r8  -> Arg4
 * r9  -> Arg5
 * r10 -> Arg6
 * Any aditional arguements should be in r11-r15.
 * No sys calls should need more than 6 args.
 */

int (*syscalls[256])(registers_t);

void setReturn(int ret) {
	asm volatile("mov %0, %%rax" :: "a"(ret));
}

void registerSyscall(int num, int (*f)(registers_t)) {

}

__attribute__((interrupt)) void syscall_handler(struct interrupt_frame* frame) {
	uint16_t syscall_num = 0;
	asm volatile("mov %%rax, %0" :: "=a"(syscall_num));
	if (syscalls[syscall_num] == NULL) {
		setReturn(-1);
		return;
	}
}

syscall_init() {
	add_interrupt_handler(0x42, syscall_handler, 0, 0x8E);
}