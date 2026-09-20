/* This file is forcefully included along with every single file.
 *
 * This is simply so that I can use these debug macros anywhere, any time, without having to with including another header and remembering to remove it when I'm done.
 */
#pragma once

#define WALLOS_PAUSE()	__asm__ volatile("pause")

#define WALLOS_HLT()        __asm__ volatile("hlt")
#define WALLOS_CLI()        __asm__ volatile("cli")
#define WALLOS_STI()        __asm__ volatile("sti")
#define WALLOS_CLI_HLT()    do { WALLOS_CLI(); WALLOS_HLT(); } while (0)
#define WALLOS_HANG()       WALLOS_CLI_HLT()

/* Meant for simple GDB debugging. Advance over this using `set $pc += 2`, then you can step through execution. */
#define WALLOS_BREAKPOINT() WALLOS_CLI(); WALLOS_HLT()

/* All interrupt handlers must be marked with both these attributes, and it's incredibly ugly to have them decorated normally. */
#define WALLOS_INTERRUPT_HANDLER __attribute__((interrupt)) __attribute__((__target__("general-regs-only")))

/* I find myself constantly needing to user this for debugging in libraries. */
#define WALLOS_RET_ADDR() __builtin_return_address(0)

#define WALLOS_STATIC_ASSERT(cond, msg) typedef char static_assert_##msg[(cond) ? 1 : -1]

/* Can be used to ensure that a function is run exactly once. */
#define WALLOS_RUN_ONCE() static int _wallos_function_already_ran = 0; if (_wallos_function_already_ran) return; _wallos_function_already_ran = 1;

#ifdef WALLOS_CPU_STATE

#include <stdint.h>
#include <drivers/serial.h>
typedef struct {
	uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
	uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
	uint64_t rip, rflags;
} CPUState;

void capture_cpu_state(CPUState* state) {
	__asm__ __volatile__(
		"mov %%rax, 0(%0)\n\t"
		"mov %%rbx, 8(%0)\n\t"
		"mov %%rcx, 16(%0)\n\t"
		"mov %%rdx, 24(%0)\n\t"
		"mov %%rsi, 32(%0)\n\t"
		"mov %%rdi, 40(%0)\n\t"
		"mov %%rbp, 48(%0)\n\t"
		"mov %%rsp, 56(%0)\n\t"
		"mov %%r8,  64(%0)\n\t"
		"mov %%r9,  72(%0)\n\t"
		"mov %%r10, 80(%0)\n\t"
		"mov %%r11, 88(%0)\n\t"
		"mov %%r12, 96(%0)\n\t"
		"mov %%r13, 104(%0)\n\t"
		"mov %%r14, 112(%0)\n\t"
		"mov %%r15, 120(%0)\n\t"
		// Capture RFLAGS
		"pushfq\n\t"
		"popq 136(%0)\n\t"
		// Capture RIP (Instruction Pointer)
		"leaq (%%rip), %%rax\n\t"
		"mov %%rax, 128(%0)\n\t"
		:
	: "r" (state)
		: "rax", "memory"
		);
}

void dump_state(const CPUState* s) {
	printf_serial("\r\n--- [ CPU STATE DUMP ] ---\r\n");
	printf_serial("General Purpose Registers:\r\n");
	printf_serial("\tRAX: 0x%016lx  RBX: 0x%016lx\r\n", s->rax, s->rbx);
	printf_serial("\tRCX: 0x%016lx  RDX: 0x%016lx\r\n", s->rcx, s->rdx);
	printf_serial("\tRSI: 0x%016lx  RDI: 0x%016lx\r\n", s->rsi, s->rdi);
	printf_serial("\tRBP: 0x%016lx  RSP: 0x%016lx\r\n", s->rbp, s->rsp);
	printf_serial("\r\nExtended Registers:\r\n");
	printf_serial("\tR8:  0x%016lx  R9:  0x%016lx\r\n", s->r8, s->r9);
	printf_serial("\tR10: 0x%016lx  R11: 0x%016lx\r\n", s->r10, s->r11);
	printf_serial("\tR12: 0x%016lx  R13: 0x%016lx\r\n", s->r12, s->r13);
	printf_serial("\tR14: 0x%016lx  R15: 0x%016lx\r\n", s->r14, s->r15);
	printf_serial("\r\nControl/Status:\r\n");
	printf_serial("\tRIP: 0x%016lx  RFLAGS: 0x%016lx\r\n", s->rip, s->rflags);
	printf_serial("--------------------------\r\n\r\n");
}
#endif