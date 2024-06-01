#include <syscall/syscall.h>

#include <system/idt.h>

/**
 * @brief We expect the arguments to come across in the
 */
void Syscall::initialize() {
	syscall_init();
}