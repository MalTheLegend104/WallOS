#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <memory/spinlock.h>

#include <scheduler/task.h>
#include <scheduler/cpu.h>

#ifdef __cplusplus
extern "C" {
#endif 

	void scheduler_init(void);

	task_t* scheduler_dequeue(cpu_t* cpu);
	void scheduler_tick(cpu_t* cpu);
	void scheduler_switch(cpu_t* cpu, task_t* prev, task_t* next);
	void scheduler_block(task_t* task);
	void scheduler_wake(task_t* task);
	void scheduler_yield(void);

	void scheduler_reap_zombie(task_t* task);
	void scheduler_terminate(task_t* task, int status);

	task_t* scheduler_find_task(pid_t pid);

	// This should probably be broken into it's own header.
	// PID can get kinda messy depending on what the context is
	// Ring 0/3? Kernel/driver? attached to specific user?
	pid_t scheduler_next_pid(void);

	/* Architecture dependant save, restore, and switch contexts. */
	void arch_save_context(struct arch_context* ctx);
	void arch_restore_context(struct arch_context* ctx) __attribute__((noreturn));
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