#ifndef WALLOS_SCHEDULER_CPU_H
#define WALLOS_SCHEDULER_CPU_H
/*
 * WFES per-CPU state.
 */
#include <scheduler/arch.h>
#include <scheduler/task.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {

#ifndef _Static_assert
#define _Static_assert static_assert
#endif
#endif

	typedef struct {
		volatile uint32_t v;
	} sched_lock_t;

	typedef struct {
		task_t* head;
		task_t* tail;
		uint32_t len;
	} task_list_t;

	struct cpu {
		/* MUST stay first. The x86_64 implementation reads these through a CPU register. */
		cpu_t* self;                       /* offset 0                                    */
		uint32_t id;                       /* offset 8: logical id (index in system_cpus) */
		uint32_t hw_id;                    /* hardware id (APIC id on x86)                */

		volatile bool online;
		volatile bool started;             /* scheduling loop entered                     */
		struct arch_cpu arch;              /* port-specific                               */

		/* Runqueues (rq_lock protects lists, counters are atomic)                        */
		sched_lock_t rq_lock;
		task_list_t rq[SCHED_PRIO_LEVELS];
		task_list_t kq;                    /* kernel tasks                                */
		volatile uint32_t nr_tasks;        /* user tasks queued (including blocked)       */
		volatile uint32_t nr_unpinned;     /* tasks of which affinity == NONE             */
		volatile uint32_t takeover;        /* >0 while a takeover kernel task exists      */

		/* owner-CPU-only state                                                           */
		task_t* current;
		task_t* idle;
		task_t* preempted;                /* task displaced by a kernel preemption        */
		task_t* zombies;                  /* destroyed, awaiting free on next entry       */
		task_t idle_task;

		uint8_t phase;                    /* 0 = P0 slot, 1 = P1 slot, 2 = normal         */
		uint8_t rr_next;                  /* next level to try, offset from P2            */
		uint8_t burst_prio;
		uint8_t burst_left;               /* remaining back-to-back runs                  */
		bool kint_used;                   /* interleaved kernel task ran this cycle       */

		uint64_t slice_start_us;
		uint32_t slice_len_us;            /* 0 = no timer (run to completion)             */

		uint32_t steal_failures;
		uint64_t cooldown_until_us;
		uint64_t next_balance_us;

		uint64_t ctx_switches;
		uint64_t timer_irqs;
	};

	// These must be here so that CPUs can access this via registers
	// Mostly an x86 thing but greatly improves hot path stuff since they can be read using a mov
	_Static_assert(offsetof(cpu_t, self) == 0, "cpu_t.self must be at offset 0");
	_Static_assert(offsetof(cpu_t, id) == 8, "cpu_t.id must be at offset 8");

	// Really shouldnt be used externally, still exposed if absolutely needed.
	extern cpu_t system_cpus[WALLOS_SYSTEM_MAX_CPU];

	// cpu_current() is much nicer than having to do arch_cpu_self(). Just a convention thing.
	static inline cpu_t* cpu_current(void) { return arch_cpu_self(); }

	// Get a gpu given it's logical id
	cpu_t* cpu_get(uint32_t logical_id);

	// Count of current CPUs. Should be used to iterate over all CPUs when needed.
	uint32_t cpu_count(void);

#ifdef __cplusplus
}
#endif
#endif // WALLOS_SCHEDULER_CPU_H