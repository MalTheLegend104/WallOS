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

void syscall_c_hook() {
	uint64_t syscall_num;
	asm volatile ("movq %%rax, %0\n\t" : "=r" (syscall_num));
	printf("Syscall %llu called.\n", syscall_num);
	// check proper syscall range.
	if (syscall_num > 256) {
		printf("Syscall %llu is out of the proper range [0,256].\n", syscall_num);
		setReturn(-1);
		return;
	}
	// make sure that syscall exists in general
	if (syscalls[syscall_num].func == NULL) {
		printf("Syscall %llu does not exist.\n", syscall_num);
		setReturn(-1);
		return;
	}
	// if we're here it does exist, we can call it
	registers_t registers;
	syscall_t current_syscall = syscalls[syscall_num];
	switch (current_syscall.arg_count) {
		case 6: asm volatile ("movq %%r10, %0\n\t" : "=r" (registers.r10));
		//fallthrough
		case 5: asm volatile ("movq %%r9, %0\n\t" : "=r" (registers.r9));
		//fallthrough
		case 4: asm volatile ("movq %%r8, %0\n\t" : "=r" (registers.r8));
		//fallthrough
		case 3: asm volatile ("movq %%rdx, %0\n\t" : "=r" (registers.rdx));
		//fallthrough
		case 2: asm volatile ("movq %%rsi, %0\n\t" : "=r" (registers.rsi));
		//fallthrough
		case 1: asm volatile ("movq %%rdi, %0\n\t" : "=r" (registers.rdi));
		//fallthrough
		default: break;
	}
}

extern void syscall_handler();

void syscall_init() {
	add_interrupt_handler_asm(0x42, syscall_handler, 0, 0x8E);
}