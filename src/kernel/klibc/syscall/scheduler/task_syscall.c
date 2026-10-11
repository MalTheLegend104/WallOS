#include <syscall/syscall.h>
#include <syscall/syscall_common.h>

#include <syscall/syscall_returns.h>
#include <wallos_attributes.h>

#include <scheduler/scheduler.h>

int sys_exit(arguments_t* args) {
	task_exit_with((int) args->arg0); // this is a noreturn

	// shouldn't be possible, still want to cover ourself
	while (true) {
		WALLOS_HLT();
	}

	WALLOS_UNREACHABLE();
}

int sys_yield(arguments_t* args) {
	(void) args;

	sched_yield();
	return E_NOERROR;
}