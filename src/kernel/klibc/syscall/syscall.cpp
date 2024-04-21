#include <syscall/syscall.h>

#include <idt.h>

/**
 * @brief We expect the arguements to come across in the
 */
void Syscall::initialize() {
	syscall_init();
}