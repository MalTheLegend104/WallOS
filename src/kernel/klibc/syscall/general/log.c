#include <memory/kernel_alloc.h>
#include <stdint.h>
#include <string.h>
#include <syscall/syscall.h>
#include <syscall/syscall_common.h>
#include <syscall/syscall_returns.h>

#include <memory/virtual_mem.h>

#include <drivers/serial.h>

#define DEBUG_LOG_MAX 512 // longest message we'll print. longer ones are truncated

int sys_debug_log(arguments_t* args) {
	// arg0 should be a const char* msg
	// arg1 is the length, we copy the string before printing it
	const char* usr_msg = (const char*) args->arg0;
	size_t msg_len = (size_t) args->arg1;

	if (msg_len == 0) return E_NOERROR;
	if (!usr_msg) return E_INVALID;

	// Never trust the length
	// We cap it, then stop at the first NUL if that comes sooner.
	if (msg_len > DEBUG_LOG_MAX) msg_len = DEBUG_LOG_MAX;

	char* msg = (char*) kcalloc(msg_len + 1, sizeof(char)); // zeroed, so the string is always terminated
	if (!msg) {
		return E_NOSPACE;
	}

	// Copies at most msg_len bytes, stops at the NUL, and returns an error for unmapped, kernel or non-readable pointers
	// There should probably be a dedicated header for things like this rather just being in the virtual mem header...
	long n = strncpy_from_user(msg, usr_msg, msg_len);
	if (n < 0) {
		kfree(msg);
		return E_INVALID;
	}
	if (n > 0) printf_serial("%s", msg);

	kfree(msg);
	return E_NOERROR;
}
