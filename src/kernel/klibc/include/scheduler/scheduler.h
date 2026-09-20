#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <memory/spinlock.h>

#include <scheduler/task.h>
#include <scheduler/cpu.h>

#ifdef __cplusplus
extern "C" {
#endif 

	/**
	 * @brief Initialize the scheduler.
	 * This will do the following:
	 *
	 *
	 */
	void scheduler_init(void);

	/**
	 * @brief Get the next task for the given CPU.
	 * Meant to allow other CPUs (without tasks) to "steal" tasks from other CPUs.
	 *
	 * @param cpu CPU to take from.
	 * @return task_t* Task stolen from another CPU, NULL if no tasks could be taken.
	 */
	task_t* scheduler_dequeue(cpu_t* cpu);

	/**
	 * @brief Scheduler tick takes care of the current CPU tasks.
	 * This *will* be run in an ISR. This *should not* be used outside an ISR.
	 *
	 * This is expected to be PER CPU tick, not a global tick.
	 *
	 * @param cpu Current CPU handle (of calling ISR)
	 */
	void scheduler_tick(cpu_t* cpu);

	/**
	 * @brief Scheduler tick takes care of the current CPU tasks, swapping if needed.
	 * This allows the use of a "global" timer, rather than a local, per CPU handler.
	 *
	 * This *will* be run in an ISR. This *should not* be used outside an ISR.
	 */
	void scheduler_tick_global();

	/**
	 * @brief Switch the currently running task on the given CPU to the provided next task.
	 * Previous is the CURRENT task that initially called this function.
	 *
	 * @param cpu Current CPU handle
	 * @param prev CURRENT Task that will be the next "previous"
	 * @param next Next task to switch to.
	 */
	void scheduler_switch(cpu_t* cpu, task_t* prev, task_t* next);

	/**
	 * @brief Mark the current task as blocked.
	 * It is expected that the caller keep track of block/wake.
	 * The scheduler does not care, and will never wake a blocked task unless told to by scheduler_wake().
	 *
	 * @param task Task to mark as blocked (will remove from runqueue)
	 */
	void scheduler_block(task_t* task);

	/**
	 * @brief Wake the task and add back to a CPU runqueue.
	 * It is expected that the caller keep track of block/wake.
	 * The scheduler will never wake a task unless explicitly told.
	 *
	 * @param task Task to wake
	 */
	void scheduler_wake(task_t* task);

	/**
	 * @brief Yield the current task to the scheduler.
	 * This will immediately switch contexts to a new task.
	 *
	 * Due to this switching contexts, it is a noreturn
	 */
	void scheduler_yield(void) __attribute__((noreturn));

	/**
	 * @brief Cleans up zombie tasks.
	 * This is going to do nothing for now (there's no real way to mark something as a zombie),
	 * mostly for future scheduler enhancement.
	 *
	 * @param task
	 */
	void scheduler_reap_zombie(task_t* task);

	/**
	 * @brief Forcefully kill a task with a given status.
	 *
	 * @param task Task to terminate
	 * @param status Status to kill task with
	 */
	void scheduler_terminate(task_t* task, int status);

	/**
	 * @brief Find the (main) task relating to a specific PID
	 *
	 * @param pid PID to search for
	 * @return task_t* Task if found, NULL otherwise.
	 */
	task_t* scheduler_find_task(pid_t pid);

	// This should probably be broken into it's own header.
	// PID can get kinda messy depending on what the context is
	// Ring 0/3? Kernel/driver? attached to specific user?
	/**
	 * @brief Get the next available PID.
	 *
	 * @return pid_t new, unique PID
	 */
	pid_t scheduler_next_pid(void);

	/* Architecture dependant save, restore, and switch contexts. */
	/**
	 * @brief Saves the current architecture context into the given struct.
	 *
	 * @param ctx Context to store into.
	 */
	void arch_save_context(struct arch_context* ctx);

	/**
	 * @brief Restore from a given context
	 *
	 * This should execute a jump/ret to the new context.
	 */
	void arch_restore_context(struct arch_context* ctx) __attribute__((noreturn));

	/**
	 * @brief Switch the contexts given two contexts.
	 */
	void arch_switch_context(struct arch_context* from, struct arch_context* to) __attribute__((noreturn));

	/**
	 * @brief Enqueue a task on a specific CPU's runqueue
	 *
	 * @param task Task to enqueue
	 * @param cpu_id Target CPU ID
	 */
	void scheduler_enqueue_on_cpu(task_t* task, uint32_t cpu_id);

	/**
	 * @brief Enqueue a task to any CPU.
	 * For now, this simply goes the CPU with the lowest tasks.
	 * (cpu->runqueue->count)
	 * @param task Task to enqueue
	 */
	void scheduler_enqueue(task_t* task);

	/**
	 * @brief Remove a task from the runqueue.
	 *
	 * @param task Task to remove
	 * @return task_t* same provided task_t* if found and removed, NULL otherwise.
	 */
	task_t* scheduler_remove_task(task_t* task);

	/**
	 * @brief Get CPU handle by ID
	 *
	 * @param cpu_id CPU identifier
	 * @return cpu_t* CPU handle, NULL if invalid ID
	 */
	cpu_t* cpu_get(uint32_t cpu_id);

	/**
	 * @brief Get total number of CPUs
	 *
	 * @return uint32_t CPU count
	 */
	uint32_t cpu_count(void);

#ifdef __cplusplus
}
#endif 