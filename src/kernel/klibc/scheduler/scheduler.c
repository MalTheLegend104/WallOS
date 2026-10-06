#include <memory/kernel_alloc.h>
#include <scheduler/sched_internal.h>
#include <string.h>

cpu_t system_cpus[WALLOS_SYSTEM_MAX_CPU];
uint32_t sched_online = 0;
volatile bool sched_ready = false;

/* Slightly cursed array to create our priority level quanta times.
 * The define is to make it readable.
 */
#define Q SCHED_QUANTUM_US
static const uint32_t prio_quanta_us[SCHED_PRIO_LEVELS] = {
	8 * Q,
	4 * Q, /* P0, P1 are capped, see spec */
	8 * Q,
	4 * Q,
	2 * Q,
	Q,
	Q / 2,
	Q / 4,
	Q / 8,
	Q / 16 /* P2-P9, halves each level */
};
#undef Q

/* Small helpers. GCC will probably inline these. Mostly just semantics to be more readable. */

cpu_t* cpu_get(uint32_t id) { return id < sched_online ? &system_cpus[id] : NULL; }
uint32_t cpu_count(void) { return sched_online; }
task_t* sched_current(void) { return cpu_current()->current; }

void sched_yield(void) { arch_yield(); }

// Wake a CPU that's idle so it notices new work right away
void sched_kick(cpu_t* target) {
	if (target == cpu_current()) return; /* Don't really know why we'd be kicking ourself, but we don't want to send an IPI to ourself. */

	// it reads target->current without a lock, so it can miss an IPI when the target is just about to go idle
	// This causes at most 1ms delay, not a huge deal, but wanted this noted
	if (target->started && target->current == target->idle) arch_send_resched_ipi(target->id);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Selection
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Whether a task can run right now.
// Pending-kill tasks aren't eligible
static inline bool eligible(uint32_t f) {
	return !(f & (TASK_F_BLOCKED | TASK_F_SUSPENDED | TASK_F_PENDING_KILL));
}

// Detach first eligible task of `l`.
// Tasks with pending-kill are pulled out and chained on *reap regardless of blocked state (so blocked tasks can be killed).
// Blocked tasks stay in place. rq_lock held.
static task_t* pick_from(cpu_t* c, task_list_t* l, task_t** reap) {
	task_t* t = l->head;
	while (t) {
		// Grab the next one first, rq_del_locked() clears the task's links
		task_t* nx = t->rq_next;
		uint32_t f = __atomic_load_n(&t->flags, __ATOMIC_ACQUIRE);
		if (f & TASK_F_PENDING_KILL) {
			// The task is off every queue now, so rq_next is free to reuse as the reap chain link
			rq_del_locked(c, t);
			t->rq_next = *reap;
			*reap = t;
		} else if (eligible(f)) {
			rq_del_locked(c, t);
			return t;
		}
		t = nx;
	}
	return NULL;
}


// Formulas 1/2/3 in spec: back_to_back = clamp(floor(len/D), 1, 8).
// `chosen` was already detached, so its queue is counted as len+1
static uint32_t back_to_back(cpu_t* c, uint32_t chosen) {
	uint32_t nonempty = 0; // P2-P9 queues with at least one task (blocked tasks count)
	uint32_t nlong = 0; // how many of those hold 20 or more tasks
	uint32_t sum = 0; // total tasks across them, used for the average
	uint32_t longest_short = 0; // longest queue that is NOT long, this is the spec's "longest_other_rq"
	uint32_t len = 0;

	for (uint32_t l = 2; l < SCHED_PRIO_LEVELS; l++) {
		uint32_t n = c->rq[l].len + (l == chosen ? 1u : 0u);
		if (l == chosen) len = n;

		if (!n) continue;

		nonempty++;
		sum += n;

		if (n >= 20) nlong++;
		else if (n > longest_short) longest_short = n;
	}

	// 3 or fewer long queues is "concentrated" load, so the divisor is the longest ordinary queue.
	// More than that is "distributed" load, so we use the average.
	// nonempty can't be 0 here because the chosen queue is always counted.
	uint32_t D = (nlong <= 3) ? longest_short : (sum / nonempty);

	// 10 is the baseline for a normal runqueue length.
	// The floor stops tiny queues from producing a huge result.
	if (D < 10) D = 10;

	// Always at least one run (normal behavior).
	// The hard cap of 8 is so no single priority level can monopolize the CPU under extreme imbalance.
	uint32_t b = len / D;
	if (b < 1) b = 1;
	if (b > 8) b = 8;
	return b;
}


// Pick from P2-P9
// Finish the current back-to-back burst if there is one, otherwise move on to the next level that has something eligible
static task_t* pick_normal(cpu_t* c, task_t** reap) {
	task_t* t;

	// Still in the middle of a burst on one level
	// If that level ran out of eligible tasks (blocked), the burst is over
	if (c->burst_left > 0) {
		t = pick_from(c, &c->rq[c->burst_prio], reap);
		if (t) {
			c->burst_left--;
			return t;
		}
		c->burst_left = 0;
	}

	// 8 levels (P2-P9), rr_next is where we resume the rotation as an offset from P2
	// Empty or fully blocked levels are skipped
	for (uint32_t i = 0; i < 8; i++) {
		uint32_t lvl = 2 + ((c->rr_next + i) % 8);
		t = pick_from(c, &c->rq[lvl], reap);
		if (!t) continue;
		c->burst_prio = (uint8_t) lvl;

		// We're already returning one task from this burst, so the rest is one less
		c->burst_left = (uint8_t) (back_to_back(c, lvl) - 1);

		// Next time start at the level after this one, wrapping from P9 back to P2
		c->rr_next = (uint8_t) ((lvl - 2 + 1) % 8);
		return t;
	}
	return NULL;
}

// Pick from the kernel queue.
// Normal kernel tasks run on any selection.
// KTASK_INTERLEAVE ones only run in the P0 slot, and only once per cycle (kint_used), otherwise they'd run back to back forever.
// A takeover lifts that restriction because nothing else is going to run anyway.
static task_t* pick_kernel(cpu_t* c, task_t** reap) {
	task_t* t = c->kq.head;

	while (t) {
		task_t* nx = t->rq_next;
		uint32_t f = __atomic_load_n(&t->flags, __ATOMIC_ACQUIRE);

		if (f & TASK_F_PENDING_KILL) {
			rq_del_locked(c, t);
			t->rq_next = *reap;
			*reap = t;
		} else if (eligible(f)) {
			bool il = t->kflags & KTASK_INTERLEAVE;

			if (!il || c->takeover || (c->phase == 0 && !c->kint_used)) {
				if (il) c->kint_used = true;
				rq_del_locked(c, t);
				return t;
			}
		}
		t = nx;
	}
	return NULL;
}


// Pick the next task. rq_lock held.
// NULL = nothing eligible (idle).
// Order: kernel tasks, the task a kernel task displaced, then the P0/P1/normal flow.
static task_t* select_locked(cpu_t* c, task_t** reap) {
	task_t* t = pick_kernel(c, reap);
	if (t) return t;
	// During a takeover only kernel tasks run
	if (c->takeover) return NULL;

	// Resume the task a kernel task displaced
	// same task, same position
	t = c->preempted;
	if (t) {
		c->preempted = NULL;
		uint32_t f = __atomic_load_n(&t->flags, __ATOMIC_ACQUIRE);
		if (f & TASK_F_PENDING_KILL) {
			t->rq_next = *reap;
			*reap = t;
		} else if (eligible(f)) {
			c->kint_used = false;
			return t;
		} else rq_add_locked(c, t, true); // It blocked or got suspended while displaced, so back to the front of its queue, it keeps its place in line.
	}

	// P0 -> P1 -> (one normal) -> P0 -> P1 -> ...
	// Three attempts because there are three slots.
	// Starting from any phase, that visits each slot once, so if any task is eligible we find it.
	for (int a = 0; a < 3; a++) {
		switch (c->phase) {
			case 0:
				c->phase = 1;
				t = pick_from(c, &c->rq[0], reap);
				break;
			case 1:
				c->phase = 2;
				t = pick_from(c, &c->rq[1], reap);
				break;
			default:
				c->phase = 0;
				t = pick_normal(c, reap);
				break;
		}
		if (t) {
			// A normal task ran, so the interleave slot is available again
			c->kint_used = false;
			return t;
		}
	}
	return NULL;
}


// How long a task gets to run before its timer fires, in microseconds.
// 0 means no timer at all.
static uint32_t quanta_for(const task_t* t) {
	if (t->is_kernel) {
		// Run to completion tasks aren't time sliced
		if (t->kflags & KTASK_RUN_TO_COMPLETION) return 0;

		// Unset means 1 quanta, and nothing gets more than the max (64 per spec)
		uint32_t q = t->kquanta ? t->kquanta : 1;
		if (q > SCHED_KERNEL_MAX_QUANTA) q = SCHED_KERNEL_MAX_QUANTA;
		return q * SCHED_QUANTUM_US;
	}
	return prio_quanta_us[t->priority];
}



// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// The Scheduling Handler
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Every task transition on a CPU goes through here: quanta expiry, yield, exit, wake IPI and kernel preemption
// `ctx` is the saved state of whatever was just interrupted, the return value is the saved state to resume
// Returning `ctx` itself means "keep running what you were running"
void* sched_handle(cpu_t* c, void* ctx, sched_reason_t reason) {
	task_t* out = c->current;

	// A reschedule IPI only wakes an idle CPU.
	// Only the kernel may end a running task's quanta early (KPREEMPT).
	if (reason == SCHED_REASON_IPI && (!out || out != c->idle)) return ctx;

	// Nothing is running yet, so there's nothing to preempt
	if (reason == SCHED_REASON_KPREEMPT && !out) return ctx;

	// A timer interrupt before this CPU entered its scheduling loop is stray
	if (reason == SCHED_REASON_TIMER && !c->started) return ctx;

	// Free anything destroyed on an earlier pass
	// This is safe now because we're not on its stack anymore
	sched_reap_zombies(c);
	uint64_t now = arch_time_us();

	/* Save Context */
	if (out) out->saved_ctx = ctx;

	task_t* reap = NULL;
	uint64_t irq = sched_lock_irqsave(&c->rq_lock);

	// The idle task never lives in a runqueue, so it's skipped
	if (out && out != c->idle) {
		uint32_t f = __atomic_load_n(&out->flags, __ATOMIC_ACQUIRE);
		if (f & TASK_F_PENDING_KILL) {
			// Destroy instead of requeue
			// It's only chained here
			// Destruction happens once the lock is dropped (cancel hooks can take their own locks)
			out->rq_next = reap;
			reap = out;
		} else if (reason == SCHED_REASON_KPREEMPT && !out->is_kernel && !c->preempted) {
			// We were displaced by a kernel task
			// Remember how much of its quanta was left so it can pick up where it stopped
			// 0 means "full quanta", which also covers tasks that had no timer
			uint64_t used = now - c->slice_start_us;
			out->remaining_us = (c->slice_len_us && used < c->slice_len_us) ? (uint32_t) (c->slice_len_us - used) : 0;
			c->preempted = out;
		} else {
			// yielded / expired
			// Send to the back of its runqueue
			// This also covers a second displacement while the slot is already taken, it just goes back in line
			rq_add_locked(c, out, false);
		}
	}

	// A takeover never resumes the `preempted` slot
	// Put it back on a queue so redistribution can move it
	// Hard affinity tasks stay
	if (c->takeover && c->preempted) {
		rq_add_locked(c, c->preempted, true);
		c->preempted = NULL;
	}
	sched_unlock_irqrestore(&c->rq_lock, irq);

	// Both of these take other CPUs locks, so they run with our own lock dropped
	if (c->takeover) sched_redistribute(c);
	if (now >= c->next_balance_us) {
		c->next_balance_us = now + SCHED_REBALANCE_US;
		sched_balance_periodic(c);
	}

	// On empty, try to steal once, else idle
	// Two passes at most
	// If the first finds nothing and the steal brought something in, the second picks it up
	task_t* next = NULL;
	for (int pass = 0; pass < 2 && !next; pass++) {
		irq = sched_lock_irqsave(&c->rq_lock);
		next = select_locked(c, &reap);
		sched_unlock_irqrestore(&c->rq_lock, irq);

		// Tasks the selection found with pending-kill (blocked ones included)
		sched_destroy_chain(reap);
		reap = NULL;

		// A taken over CPU never steals
		if (next || c->takeover) break;
		if (pass == 0 && !sched_idle_try_steal(c, now)) break;
	}
	if (!next) next = c->idle;

	// Re-arm timer

	c->current = next;
	uint32_t us;

	// Idle just needs to wake up often enough to notice work and retry stealing
	if (next == c->idle) us = SCHED_IDLE_WAKE_US;
	else if (next->remaining_us) {
		// Resuming a displaced task, it only gets what it had left
		us = next->remaining_us;
		next->remaining_us = 0;
	} else us = quanta_for(next);

	c->slice_len_us = us;
	c->slice_start_us = arch_time_us();
	if (us) {
		// Low resolution fallback
		// A timer can't fire sooner than its minimum interval, we fire at whatever the timer can handle
		uint32_t m = arch_timer_min_us();
		arch_timer_arm_oneshot_us(us < m ? m : us);
	} else {
		arch_timer_disarm();
	}

	// We now have work, so stop advertising ourselves for donations
	if (next != c->idle) sched_clear_targetable(c);
	if (next != out) c->ctx_switches++;

	return next->saved_ctx;
}


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Idle Task and Bring-up
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Idle task does nothing
// All scheduling stuff is done in the timer interrupt
static void sched_idle_main(void* unused) {
	(void) unused;
	for (;;) arch_halt();
}

// Runs on the BSP once every AP is up
// Sets up the idle task of every CPU, then releases the APs waiting in sched_ap_entry()
void sched_init(uint32_t n) {
	if (n > WALLOS_SYSTEM_MAX_CPU) n = WALLOS_SYSTEM_MAX_CPU;
	sched_online = n;

	for (uint32_t i = 0; i < n; i++) {
		cpu_t* c = &system_cpus[i]; // self/id/hw_id set by the port at bind
		task_t* idle = &c->idle_task;

		memset(idle, 0, sizeof *idle);

		// The idle task isn't created through task_create(), so it has no id and isn't in the registry
		idle->id = TASK_ID_INVALID;
		idle->priority = SCHED_PRIO_KERNEL;
		idle->is_kernel = true;
		idle->cpu = i;

		// It only ever halts, so it needs very little stack
		idle->stack_size = 16 * 1024;
		idle->stack_base = kalloc(idle->stack_size);
		idle->owns_stack = true;
		memcpy(idle->name, "idle", 5);
		idle->saved_ctx = arch_task_context_init(idle->stack_base, idle->stack_size, sched_idle_main, NULL);
		c->idle = idle;
		c->online = true;
	}

	// Release, so the APs see the idle tasks fully set up once they see this flag
	__atomic_store_n(&sched_ready, true, __ATOMIC_RELEASE);
}

// Start preemption on the BSP
// The thread that ran the boot code has to adopt that code path as a task first (sched_adopt_boot_ta)
void sched_start_bsp(void) {
	cpu_t* c = cpu_current();

	// The timer must not fire while we're half set up
	uint64_t f = arch_irq_save();
	c->started = true;
	c->slice_start_us = arch_time_us();
	c->next_balance_us = c->slice_start_us + SCHED_REBALANCE_US;

	// Fall back to the idle wake interval if nothing was adopted
	c->slice_len_us = c->current ? quanta_for(c->current) : SCHED_IDLE_WAKE_US;
	if (c->slice_len_us) arch_timer_arm_oneshot_us(c->slice_len_us);
	arch_irq_restore(f);
}

// Where every AP ends up after bringup
// Never returns
__attribute__((noreturn)) void sched_ap_entry(void) {
	// The idle tasks don't exist until the BSP has finished sched_init()
	while (!__atomic_load_n(&sched_ready, __ATOMIC_ACQUIRE)) arch_cpu_relax();

	cpu_t* c = cpu_current();

	// Stay masked until the first switch, which restores the interrupt state from the new task's frame
	// That's why the returned state is never restored here
	arch_irq_save();

	// The bring-up context is thrown away
	// With nothing "running", the handler has nothing to save
	c->current = NULL;
	c->started = true;
	c->next_balance_us = arch_time_us() + SCHED_REBALANCE_US;

	// Trap into the handler to enter the idle task (or whatever task we were potentially handed)
	arch_yield();

	// Not reached
	for (;;) arch_halt();
	__builtin_unreachable();
}

// Every task starts here (arch_task_context_init points its first frame at it)
// Returning from the entry point is a normal exit
// The boot code that is inherited as a path needs to manually call task_exit().
void sched_task_bootstrap(task_entry_t fn, void* arg) {
	fn(arg);
	task_exit();
}
