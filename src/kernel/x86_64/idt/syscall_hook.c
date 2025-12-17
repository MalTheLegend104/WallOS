#include <syscall/syscall.h>
#include <system/idt.h>
#include <stdio.h>
#include <stddef.h>
#include <memory/kernel_alloc.h>
/* All of our actual interrupt handling is going to be dispatched through here. */

/* We push the arguments in a specific order
 * This follows the System V ABI calling convention, with the notable exception of rax being used.
 * The first few are pushed into normal registers, the rest are in the extended registers
 * rax -> Syscall Number
 * rdi -> Arg1
 * rsi -> Arg2
 * rdx -> Arg3
 * rcx -> Arg4
 * r8  -> Arg5
 * r9  -> Arg6
 * Any additional arguments should be in r10-r16.
 * Really they should be on the stack, but I'm too lazy for that.
 * No sys calls should need more than 6 args anyway.
 */

syscall_t syscalls[256];

void setReturn(int ret) {
	asm volatile("mov %0, %%eax" :: "a"(ret) : );
}

void registerSyscall(int syscall_num, int (*f)(registers_t*), uint8_t arg_count) {
	if (syscall_num > 256 || syscall_num < 0) return;
	syscalls[syscall_num].func = f;
	syscalls[syscall_num].arg_count = arg_count;
}

void syscall_c_hook(registers_t* regs) {

	uint64_t syscall_num = regs->rax;
	// printf("Syscall %llu called.\n", syscall_num);
	// check proper syscall range.
	if (syscall_num > 256) {
		printf("Syscall %llu is out of the proper range [0,256].\n", syscall_num);
		kfree(regs);
		setReturn(-1);
		return;
	}
	// make sure that syscall exists in general
	if (syscalls[syscall_num].func == NULL) {
		printf("Syscall %llu does not exist.\n", syscall_num);
		kfree(regs);
		setReturn(-1);
		return;
	}
	syscall_t current_syscall = syscalls[syscall_num];
	int ret = current_syscall.func(regs);
	kfree(regs);
	setReturn(ret);
}

int syscall_test(registers_t* regs) {
	printf("RAX: %llu\nRDI: %llu\nRSI: %llu\nRDX: %llu\nRCX: %llu\nR8:  %llu\nR9: %llu\n",
		regs->rax, regs->rdi, regs->rsi, regs->rdx, regs->rcx, regs->r8, regs->r9);
	return 0;
}

extern void syscall_handler();

void syscall_init() {
	add_interrupt_handler_asm(0x42, syscall_handler, 0, 0x8E);
	registerSyscall(1, syscall_test, 1);
}