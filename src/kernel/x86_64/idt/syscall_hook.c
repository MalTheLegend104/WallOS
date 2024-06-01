#include <syscall/syscall.h>
#include <system/idt.h>
#include <stdio.h>
#include <stddef.h>

/* All of our actual interrupt handling is going to be dispatched through here. */

/* We push the arguments in a specific order
 * The first few are pushed into normal registers, the rest are in the extended registers
 * rax -> Syscall Number
 * rdi -> Arg1
 * rsi -> Arg2
 * rdx -> Arg3
 * r8  -> Arg4
 * r9  -> Arg5
 * r10 -> Arg6
 * Any additional arguments should be in r11-r15.
 * No sys calls should need more than 6 args.
 */

syscall_t syscalls[256];

void setReturn(int ret) {
	asm volatile("mov %0, %%eax" :: "a"(ret) : );
}

void registerSyscall(int syscall_num, int (*f)(registers_t), uint8_t arg_count) {
	if (syscall_num > 256 || syscall_num < 0) return;
	syscalls[syscall_num].func = f;
	syscalls[syscall_num].arg_count = arg_count;
}

void syscall_c_hook(uint64_t syscall_num) {
	//assert(syscall_num);
	printf("Syscall %llu called.\n", syscall_num);
	if (syscalls[syscall_num].func == NULL) {
		printf("Syscall %llu does not exist.\n", syscall_num);
		setReturn(-1);
		return;
	}
}

extern void syscall_handler();

void syscall_init() {
	add_interrupt_handler_asm(0x42, syscall_handler, 0, 0x8E);
}