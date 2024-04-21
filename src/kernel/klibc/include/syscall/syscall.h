#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#define SYSCALL_LOG 1

#ifdef __cplusplus
namespace Syscall {
	void initialize();
}
extern "C" {
#endif 
#include <stdint.h>
	typedef struct {

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