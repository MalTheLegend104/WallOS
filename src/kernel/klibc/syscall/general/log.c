#include <memory/kernel_alloc.h>
#include <stdint.h>
#include <string.h>
#include <syscall/syscall.h>
#include <syscall/syscall_common.h>
#include <syscall/syscall_returns.h>

#include <drivers/serial.h>

int sys_debug_log(arguments_t* args) {
	// arg0 should be a const char* msg
	// arg1 is the length, we copy the string before printing it
	const char* usr_msg = (const char*) args->arg0;
	size_t msg_len = (size_t) args->arg1;

	if (msg_len == 0) return E_NOERROR;

	char* msg = (char*) kcalloc(msg_len + 1, sizeof(char));
	strncpy(msg, usr_msg, msg_len); // TODO: should have some form of copy_from_user

	printf_serial("%s", msg);

	kfree(msg);
	return E_NOERROR;
}