#pragma once

#include <stdint.h>

/* this is intentionally a forward declare.
 * the actual architecture context struct should be defined in:
 *     src/kernel/<arch>/scheduler/
 *
 * The scheduler doesn't particularly care about *how* CPU context is described.
 * It just needs to be able to tell the architecture implementation "save current context" and "switch to this one"
 * with struct pointers.
 */
struct arch_context;

typedef uint32_t pid_t;

/* Basic task tracking, hopefully lets us make "smarter" scheduling later on. */
typedef enum {
	TASK_RUNNING,
	TASK_READY,
	TASK_BLOCKED,
	TASK_ZOMBIE
} task_state_t;

/* Attempt at supporting user mode context switching in the future. */
typedef enum {
	TASK_KERNEL,
	TASK_USER
} task_type_t;

typedef struct task {
	struct arch_context* arch;
	void* kernel_stack;
	void* user_stack;

	task_state_t state;
	task_type_t type;

	pid_t pid;

	uint32_t cpu_affinity;
	uint32_t current_cpu;

	uint32_t timeslice;
	uint32_t priority;

	struct task* prev;
	struct task* next;
} task_t;


/**
 * @brief Exits the current task with the given status.
 */
void task_exit(int status) __attribute__((noreturn));