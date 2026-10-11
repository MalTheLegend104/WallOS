#ifndef WALLOS_WFES_ARCH_H
#define WALLOS_WFES_ARCH_H
/*
 * WFES architecture interface.
 *
 * The core scheduler only talks to hardware through the functions below.
 * A port must provide them, plus `struct arch_cpu` (embedded in cpu_t).
 * Functions marked INLINE must be static inline in the port's arch_cpu.h (they are on hot paths)
 * The rest are ordinary functions.
 *
 * Context model:
 *     A "context" is whatever the port needs to resume a task.
 *     The core only sees an opaque pointer (task->saved_ctx).
 *     When the timer/yield/IPI trap fires, the port saves the interrupted state, calls sched_handle(cpu, ctx, reason) and resumes whatever context pointer it returns.
 *     Returning the same pointer resumes the interrupted task.
 */
#include <scheduler/task.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef enum {
		SCHED_REASON_TIMER,     /* quanta expiry / idle wake                    */
		SCHED_REASON_YIELD,     /* task yielded or exited                       */
		SCHED_REASON_IPI,       /* reschedule IPI: only acted on by an idle CPU */
		SCHED_REASON_KPREEMPT,  /* kernel preemption: interrupts anything       */
	} sched_reason_t;

	/* These are implemented by the core */
	void* sched_handle(cpu_t* cpu, void* ctx, sched_reason_t reason);
	void sched_task_bootstrap(task_entry_t fn, void* arg) __attribute__((noreturn));

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Everything below here needs to be defined by the architecture
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/* INLINE in arch_cpu.h:
	 *   cpu_t*   arch_cpu_self(void);          this CPU's cpu_t
	 *   uint32_t arch_cpu_id(void);            logical id
	 *   uint64_t arch_irq_save(void);          disable IRQs, return previous state
	 *   void     arch_irq_restore(uint64_t);
	 *   void     arch_cpu_relax(void);
	 *   void     arch_halt(void);              low power wait for interrupt
	 */

	uint64_t arch_time_us(void);                    /* monotonic, per-CPU valid */
	uint32_t arch_timer_min_us(void);               /* smallest usable interval */
	void arch_timer_arm_oneshot_us(uint32_t us);    /* this CPU                 */
	void arch_timer_disarm(void);
	void arch_send_resched_ipi(uint32_t cpu_id);
	void arch_send_kpreempt_ipi(uint32_t cpu_id);
	void arch_yield(void);                          /* trap into sched_handle   */

	/**
	 * Build a context that, when first resumed, calls sched_task_bootstrap(fn,arg).
	 * Returns NULL if the stack is too small to hold the initial context.
	 */
	void* arch_task_context_init(void* stack_base, size_t stack_size, task_entry_t fn, void* arg);

	/* Called from sched_handle, IRQs off, when the CPU is about to resume a different task than the one it interrupted.
	 * `out` is NULL the first time a CPU enters its scheduling loop.
	 * The port switches whatever is per-task but not part of the saved context (address space, kernel stack pointers for traps, etc.)
	 */
	void arch_task_switch(task_t* out, task_t* next);

	/**
	 * Drop the calling task into user mode at `entry` with the given user stack and address space.
	 * Never returns.
	 * The task keeps its kernel stack, which is used for every later trap from user mode.
	 */
	void arch_enter_user(uintptr_t entry, uintptr_t user_sp, uint64_t aspace) __attribute__((noreturn));

	/**
	 * Initializes all CPUs on a system, and starts the scheduler.
	 * The BSP will inherit it's current code path as a task.
	 * The BSP code path should exit as a normal task after all other creation is done.
	 */
	void arch_init_cpus();

#ifdef __cplusplus
}
#endif

#if defined(__x86_64__)
#include <x86_64/arch_cpu.h>
#else
#error "WFES: no architecture port for this target"
#endif

#endif // WALLOS_WFES_ARCH_H