#ifndef WALLOS_SYSCALL_COMMON_H
#define WALLOS_SYSCALL_COMMON_H

// This file is for all the sys_* entries that are in the common range.
// This is for both the syscall signatures, and the table created from them that is registered in syscall_init()

#include <syscall/syscall.h>
#include <syscall/syscall_nums.h>

#ifdef __cplusplus
extern "C" {
#endif

	// Add the syscall function names here. Try to keep them in order of their numbers.
	// SYSCALL(<name>) expects the signature of `int sys_<name>(arguments_t* args)`
	SYSCALL(exit);
	SYSCALL(yield);
	SYSCALL(debug_log);

	// Every common syscall. Number and name are written out here, one row each.
	// Only use SYSCALL_DEF when adding to this table.
	// SYSCALL_DEF(<number>, <name>, <arg_count>)
	static const syscall_def_t common_syscalls[] = {
		SYSCALL_DEF(SYS_EXIT, exit, 1),
		SYSCALL_DEF(SYS_YIELD, yield, 0),
		SYSCALL_DEF(SYS_DEBUG_LOG, debug_log, 2),
	};

#ifdef __cplusplus
}
#endif
#endif // WALLOS_SYSCALL_COMMON_H