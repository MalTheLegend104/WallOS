#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#define SYSCALL_LOG 1

#ifdef __cplusplus
extern "C" {
#endif 

	typedef struct {

	} registers_t;

	typedef struct {
		int (*func)(registers_t);
		uint8_t arg_count;
	} syscall_t;

#ifdef __cplusplus
}
#endif 
#endif // KERNEL_SYSCALL_H