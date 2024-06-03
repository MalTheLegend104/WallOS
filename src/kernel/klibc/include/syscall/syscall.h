#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#define SYSCALL_LOG 1

#include <stdint.h>

#ifdef __cplusplus
namespace Syscall {
	void initialize();
}
extern "C" {
#endif 
#include <stdint.h>
	typedef uint64_t rxx;
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
	typedef struct {
		rxx rax;
		rxx rdi;
		rxx rsi;
		rxx rdx;
		rxx r8;
		rxx r9;
		rxx r10;
	} registers_t;

	typedef struct {
		int (*func)(registers_t);
		uint8_t arg_count;
	} syscall_t;

	void registerSyscall(int syscall_num, int (*f)(registers_t), uint8_t arg_count);
	void syscall_init();
#ifdef __cplusplus
}
#endif 
#endif // KERNEL_SYSCALL_H