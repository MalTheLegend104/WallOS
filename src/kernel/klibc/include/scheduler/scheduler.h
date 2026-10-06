#ifndef WALLOS_SCHEDULER_H
#define WALLOS_SCHEDULER_H
#include <scheduler/cpu.h>
#include <scheduler/task.h>

#ifdef __cplusplus
extern "C" {
#endif

	/* Bring-up (called from arch CPU init) */
	void sched_init(uint32_t cpu_count);                 /* BSP: after all APs are up   */
	task_t* sched_adopt_boot_task(const char* name);     /* BSP: current thread -> task */
	void sched_start_bsp(void);                          /* BSP: begin preemption */
	void sched_ap_entry(void) __attribute__((noreturn)); /* APs: never returns */

	/* Running tasks */
	task_t* sched_current(void);
	void sched_yield(void);

	task_id_t task_create(const task_create_info_t* info);
	task_id_t task_spawn(const char* name, task_entry_t entry, void* arg);
	void task_exit(void) __attribute__((noreturn));
	bool task_kill(task_id_t id); /* External. Applied at next quanta boundary */
	bool task_suspend(task_id_t id);
	bool task_resume(task_id_t id);

	/* Blocking subsystem contract (spec: Blocking Event Handlers)
	 *
	 * Registration:  task_block_begin(t, cancel, ctx) sets the blocked flag and records the cancel hook.
	 *                The subsystem records t in its waitlist.
	 *
	 * Delivery:      Deliver result, then task_wake(t), then drop t from the waitlist.
	 *                task_wake() returns false if the task is being destroyed, the delivery must then be discarded.
	 *                DO NOT touch `t` after task_wake() returns.
	 *
	 * Cancellation:  Destruction calls cancel(t, ctx) if the task was blocked.
	 *                It must remove `t` from the subsystem's waitlist (Idempotent. It can race with a delivery).
	 */
	void task_block_begin(task_t* t, task_cancel_fn cancel, void* ctx);
	bool task_wake(task_t* t);

	/**
	 * @brief Wake by id
	 * Safe from IRQ / timer-callback context, where holding a raw task_t* would be a use-after-free.
	 *
	 * @return false if the task no longer exists or is dying
	 */
	bool task_wake_id(task_id_t id);

	/**
	 * @brief Wait until this task's blocked flag is cleared. Second half of a block.
	 *
	 * Use it after task_block_begin() when you need the flag set *while you still hold your own lock* (the race-free pattern, see sched_block_current()).
	 * Returns after task_wake() has been called for this task.
	 * If the task is killed while waiting, this never returns.
	 */
	void sched_block_wait(void);

	/**
	 * @brief Block the calling task until another context wakes it with task_wake().
	 *
	 * Sets this task's blocked flag, records @p cancel / @p ctx as its cancel hook, then yields until the flag clears.
	 * While blocked the scheduler skips the task without removing it from its runqueue, so it costs nothing but its queue slot.
	 *
	 * @param cancel  Called if the task is destroyed while blocked (killed, or a parent died).
	 *                It must remove the task from your waitlist and be idempotent, since it can race with a delivery.
	 *                May be NULL if nothing holds a reference to the task.
	 * @param ctx     Passed back to @p cancel unchanged.
	 *
	 * @note Must be called from task context, never from an interrupt handler or with interrupts disabled (it yields).
	 * The task resumes at its next natural turn, not immediately.
	 */
	void sched_block_current(task_cancel_fn cancel, void* ctx);

	// Using sched_block_current and sched_block_wait:
	//
	// // waiter
	// lock(&dev->lock);
	// if (dev->ready) { unlock(&dev->lock); return; }   // re-check before sleeping
	// waitlist_add(&dev->waiters, sched_current());
	// unlock(&dev->lock);
	// sched_block_current(dev_cancel, dev);
	//
	// // waker (IRQ or task context)
	// lock(&dev->lock);
	// dev->ready = true;
	// task_t* t = waitlist_pop(&dev->waiters); // remove from the list first...
	// unlock(&dev->lock);
	// if (t) task_wake(t); // ...and never touch t afterwards
	//
	// Lost-wakeup window:
	//     The blocked flag is set inside this call, so a task_wake() that runs after waitlist_add() but before this call finds nothing to clear and is dropped.
	//     The task then sleeps forever.
	//     Use sched_block_current() only when the waker cannot run before you get here (for example it is triggered by something you start afterwards,
	//     or your condition is re-checked in a loop with a timeout).
	//
	// Otherwise:
	//
	// lock(&dev->lock);
	// task_block_begin(sched_current(), dev_cancel, dev); // flag set under the lock
	// waitlist_add(&dev->waiters, sched_current());
	// unlock(&dev->lock);
	// sched_block_wait(); // now a wake cannot be lost
	//
	//

#ifdef __cplusplus
}
#endif
#endif // WALLOS_SCHEDULER_H