#ifndef WALLOS_X86_64_ARCH_TASK_H
#define WALLOS_X86_64_ARCH_TASK_H
#include <memory/virtual_mem.h>
#include <stdbool.h>
#include <stdint.h>

// This gets embedded at the end of task_t in task.h
struct arch_task {
	bool user_mode;   //< has dropped to ring 3 at least once. Enables the per-switch user GS/FS handling
	uint64_t user_gs; //< user GS base. lives in KERNEL_GS_BASE while the task is in kernel mode
	uint64_t user_fs; //< user FS base (TLS). not swapped by swapgs, so saved/restored by hand
	address_space_t* as; //< address space. 0 means kernel only
};
#endif
