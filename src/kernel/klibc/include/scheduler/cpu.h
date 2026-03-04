#ifndef SCHEDULER_CPU_H
#define SCHEDULER_CPU_H

#include <stdint.h>
#include <memory/spinlock.h>
#include <scheduler/task.h>

#ifdef __cplusplus
extern "C" {
#endif 

	typedef struct runqueue {
		task_t* head;
		task_t* tail;
		uint32_t count;
	} runqueue_t;

	typedef struct cpu {
		uint32_t id;
		task_t* current;
		task_t* idle_task;
		runqueue_t* run_queue;
		spinlock_t rq_lock;
	} cpu_t;

	typedef void (*task_entry_t)(void);

	// These are all architecture dependant.

	/**
	 * @brief Create a task with the given entry point.
	 *
	 * Due to the context switching being platform dependant, we need this to be platform dependant.
	 *
	 * @param entry_point Pointer to the entry function.
	 * @param is_user True if it's a user task, false otherwise.
	 * @return task_t* Pointer to the new task, NULL if it couldn't be created for some reason.
	 */
	task_t* task_create(task_entry_t* entry_point, bool is_user);

	/**
	 * @brief Get the handle the CURRENT cpu
	 *
	 * @return cpu_t* handle to the CPU
	 */
	cpu_t* cpu_current(void);

	/**
	 * @brief Default "task 0" that runs when the CPU has absolutely nothing else to do.
	 * It's just a while (true) { hlt(); }
	 *
	 */
	void idle_task_main(void);

	/**
	 * @brief Tells the architecture implementation to "initialize" all the CPUs.
	 * This should be called before all other calls to any other cpu_* functions.
	 *
	 * It is expected that this only "loads" the CPUs, which includes:
	 * Setting up each CPU to a state where it can be loaded with a task.
	 * Should be in a "waiting" state.
	 */
	void arch_init_cpus();

#ifdef __cplusplus
}
#endif 

#endif /* SCHEDULER_CPU_H */ // carlos was here - carlos