#include <scheduler/sched_internal.h>

// This file has everything to do with CPU runqueue balancing and stealing.
// I have neglected to write the SMP Sync section of the spec, so I just kinda implemented what was easiest.
//
// - Every CPU has a runqueue with it's own lock. The owner takes it on it's scheduling pass.
// - A CPU never holds two runqueues at once.
//     - Moving a task:
//         1. trylock() source
//         2. Detach the tasks into a private chain
//         3. Unlock the source
//         4. lock the destination
//         5. attach the task(s)
//         6. unlock dest
// We trylock on the source so a busy "victim" is skipped so we can avoid a deadlock
//
// Killing never touches another CPU at all. Sets an atomic flag on the task that the other CPU will check before scheduling it.
// Stealing CPUs will ignore PENDING_KILL tasks.
//
// Donation is a global bitmask based on logical cpu_id. Donor claims a CPU by clearing it's bit atomically.

#define MASK_WORDS ((WALLOS_SYSTEM_MAX_CPU + 63) / 64)
static volatile uint64_t idle_mask[MASK_WORDS];

// There's a lot of GCC __atomic_ calls below.
// I'm sorry to whoever reads this.
// It's just all setting and removing bit flags.


void sched_set_targetable(cpu_t* c) {
	volatile uint64_t* w = &idle_mask[c->id / 64];
	uint64_t bit = 1ull << (c->id % 64);
	if (!(__atomic_load_n(w, __ATOMIC_RELAXED) & bit)) __atomic_fetch_or(w, bit, __ATOMIC_ACQ_REL);
}

void sched_clear_targetable(cpu_t* c) {
	volatile uint64_t* w = &idle_mask[c->id / 64];
	uint64_t bit = 1ull << (c->id % 64);
	if (__atomic_load_n(w, __ATOMIC_RELAXED) & bit) __atomic_fetch_and(w, ~bit, __ATOMIC_ACQ_REL);
}

int32_t sched_claim_idle_cpu(uint32_t exclude) {
	for (uint32_t w = 0; w < MASK_WORDS; w++) {
		uint64_t v = __atomic_load_n(&idle_mask[w], __ATOMIC_RELAXED);
		while (v) {
			uint32_t b = (uint32_t) __builtin_ctzll(v);
			uint32_t id = w * 64 + b;
			uint64_t bit = 1ull << b;
			if (id != exclude && id < sched_online) {
				/* only the donor that flips the bit wins */
				if (__atomic_fetch_and(&idle_mask[w], ~bit, __ATOMIC_ACQ_REL) & bit) return (int32_t) id;
			}
			v &= ~bit;
		}
	}
	return -1;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Moving Tasks
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Check if a task is even movable.
// Checks for non-soft affinity first, then checks the soft affinity steal conditions.
static bool movable(const task_t* t, int soft, bool blocked_ok, uint32_t load_left) {
	uint32_t f = __atomic_load_n(&t->flags, __ATOMIC_ACQUIRE);
	if (f & (TASK_F_PENDING_KILL | TASK_F_DYING)) return false;
	if (!blocked_ok && (f & (TASK_F_BLOCKED | TASK_F_SUSPENDED))) return false;
	if (t->is_kernel) return false;
	switch (t->affinity) {
		case AFFINITY_NONE: return true;
		case AFFINITY_SOFT:
			if (soft == MOVE_SOFT_ALWAYS) return true;
			if (soft == MOVE_SOFT_IF_COMPANY) return load_left >= 2; /* not the only task */
			return false;
		default: return false; /* hard affinity, never */
	}
}

// Take up to `max` tasks from the back of the `from` queues
// P0/P1 are not checked, not worth stealing them
// Returns how many tasks actually moved.
uint32_t sched_move_tasks(cpu_t* from, cpu_t* to, uint32_t max, int soft, bool blocked_ok) {
	if (from == to || !max || !from->nr_tasks) return 0;

	task_t *chain = NULL, *tail = NULL;
	uint32_t n = 0;
	uint64_t f;

	if (!sched_trylock_irqsave(&from->rq_lock, &f)) return 0;

	// Get the cpu_load value from the CPU we are considering stealing from
	uint32_t load = cpu_load(from);

	// Check all the levels for tasks we can steal
	for (uint32_t lvl = 2; lvl < SCHED_PRIO_LEVELS && n < max; lvl++) {
		task_t* t = from->rq[lvl].tail;
		while (t && n < max) {
			task_t* prev = t->rq_prev;

			// If it's movable, we take it off the queue and add it to our "steal chain"
			if (movable(t, soft, blocked_ok, load - n)) {
				rq_del_locked(from, t);
				t->rq_next = NULL;
				if (tail) tail->rq_next = t;
				else chain = t;
				tail = t;
				n++;
			}
			t = prev;
		}
	}
	sched_unlock_irqrestore(&from->rq_lock, f);
	if (!n) return 0;

	f = sched_lock_irqsave(&to->rq_lock);
	while (chain) {
		task_t* nx = chain->rq_next;
		rq_add_locked(to, chain, false);
		chain = nx;
	}
	sched_unlock_irqrestore(&to->rq_lock, f);

	// Wake the destination CPU in case it was idle and doesn't know it has work
	sched_kick(to);
	return n;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Stealing
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Check if a soft affinity task is allowed to be stolen
// Read spec to see the conditions
static bool soft_steal_allowed(const cpu_t* thief) {
	if (cpu_load(thief) != 0) return false; /* thief has nothing else */
	for (uint32_t i = 0; i < sched_online; i++) {
		const cpu_t* c = &system_cpus[i];
		if (c == thief) continue;
		if (c->nr_unpinned) return false; /* others: only affinity */
		const task_t* cur = c->current;
		if (cur && cur != c->idle && !cur->is_kernel && cur->affinity == AFFINITY_NONE) return false;
	}
	return true;
}

// An idle CPU pulls work from other CPUs.
// Visits the other CPUs round-robin starting just after the thief (so thieves don't all hammer CPU 0).
// It takes about half of each victim's load, up to SCHED_STEAL_BATCH total.
// Returns the number of tasks stolen.
uint32_t sched_steal(cpu_t* thief) {
	// Whether soft affinity tasks are fair game depends only on system state, not on which victim we pick
	int soft = soft_steal_allowed(thief) ? MOVE_SOFT_IF_COMPANY : MOVE_SOFT_NEVER;
	uint32_t total = 0;

	for (uint32_t i = 1; i < sched_online && total < SCHED_STEAL_BATCH; i++) {
		cpu_t* v = &system_cpus[(thief->id + i) % sched_online];

		// Nothing queued means nothing to take
		if (!v->online || !v->nr_tasks) continue;

		// Half the victim's load leaves both CPUs about even
		uint32_t want = cpu_load(v) / 2;
		if (!want) want = 1;

		// Never take more than what's left of the batch, so one stealing pass stays short
		if (want > SCHED_STEAL_BATCH - total) want = SCHED_STEAL_BATCH - total;

		total += sched_move_tasks(v, thief, want, soft, false);
	}
	return total;
}

// Steal cooldown after `failures` consecutive failed attempts
static uint64_t cooldown_us(uint32_t failures) {
	if (!failures) return 0;

	// The shift is clamped to 16 only so the shift itself can't overflow a 64-bit value. It has no effect on the actual result.
	// With the default 16 ms base the 1 s cap is already reached at the 7th failure (16 ms << 6 = 1.024 s)
	uint32_t sh = failures - 1;
	if (sh > 16) sh = 16;
	uint64_t v = (uint64_t) SCHED_STEAL_COOLDOWN_BASE_US << sh;
	return v > SCHED_STEAL_COOLDOWN_CAP_US ? SCHED_STEAL_COOLDOWN_CAP_US : v;
}

// Called from the scheduling handler when a CPU has nothing to run.
// Returns true if the runqueue now (probably) has work.
//
// The idle backoff works like this:
//     1. If we're "cooling down" from a recent failure, just advertise that we can take donations and go back to idle.
//     2. Otherwise stop accepting donations and try to steal.
//     3. On failure, start (or lengthen) a cooldown and advertise again.
bool sched_idle_try_steal(cpu_t* c, uint64_t now) {
	if (sched_online < 2) return false;

	// Still cooling down from a failed attempt
	if (now < c->cooldown_until_us) {
		sched_set_targetable(c);
		return false;
	}

	// We're about to steal, so clear our bit first, so we dont get donataions
	sched_clear_targetable(c);
	// A donor may have claimed us right before we cleared the bit
	if (c->nr_tasks) return true;

	// try to steal and reset cooldown if we did
	if (sched_steal(c)) {
		c->steal_failures = 0;
		return true;
	}

	// cooldown_us() already clamps its shift at 16
	// The cap just keeps the counter from wrapping on a CPU that stays idle for a very long time
	if (c->steal_failures < 32) c->steal_failures++;
	c->cooldown_until_us = now + cooldown_us(c->steal_failures);
	sched_set_targetable(c);
	return false;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Periodic Rebalancing
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Runs on every CPU every SCHED_REBALANCE_US, from its own scheduling handler.
// Finds the busiest and the least busy CPU.
// If the gap is big enough, the busiest one offers tasks to the emptiest one,
// or if we are the one that's sufficiently empty, we pull from the busiest instead.
void sched_balance_periodic(cpu_t* me) {
	if (sched_online < 2) return;
	if (me->takeover) return;

	cpu_t *max = NULL, *min = NULL;
	uint32_t maxl = 0, minl = 0xFFFFFFFFu;


	for (uint32_t i = 0; i < sched_online; i++) {
		cpu_t* c = &system_cpus[i];

		// A CPU that is offline or taken over doesn't run normal tasks, so it can neither give nor receive any
		if (!c->online || c->takeover) continue;

		uint32_t l = cpu_load(c);
		if (l >= maxl) {
			maxl = l;
			max = c;
		}
		if (l < minl) {
			minl = l;
			min = c;
		}
	}

	// max == min happens when every CPU has the same load
	// SCHED_IMBALANCE_MIN keeps us from shuffling tasks back and forth over a small difference
	if (!max || !min || max == min || maxl - minl < SCHED_IMBALANCE_MIN) return;

	uint32_t n = (maxl - minl) / 2; // each CPU should get roughly half the load

	// This cap is arbitary, mostly just to make sure we don't have the lock for a super long time
	if (n > 8) n = 8;

	if (me == max) {                                    /* offer */
		sched_move_tasks(me, min, n, MOVE_SOFT_NEVER, false);
	} else {

		// We only pull if we're sufficiently empty, meaning at most half the busiest CPU's load.
		// Otherwise it's not our job to fix the imbalance.
		uint32_t mine = cpu_load(me);
		if (mine * 2 <= maxl) {
			// Recompute against our own load, since we may not be the emptiest CPU
			n = (maxl - mine) / 2;
			if (n > 8) n = 8;
			sched_move_tasks(max, me, n, MOVE_SOFT_NEVER, false);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// CPU Takeover
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Everything except hard affinity (and kernel) tasks leaves the CPU.
// It is retried on every scheduling pass while the takeover lasts, since a trylock on a busy destination can fail.
void sched_redistribute(cpu_t* c) {
	// nowhere to send anything on a single CPU
	if (sched_online < 2) return;

	// Starts at 1 so we never pick ourselves, and goes round-robin from the CPU right after us.
	// This stops every taken-over CPU from dumping its work on CPU 0.
	// Hard affinity tasks can't move, so nr_tasks may never reach 0. This is fine because the loop ends after one lap.
	for (uint32_t i = 1; i < sched_online && c->nr_tasks; i++) {
		cpu_t* d = &system_cpus[(c->id + i) % sched_online];

		// We dont want to dump work to a CPU that is itself taken over
		if (!d->online || d->takeover) continue;

		// An even share for each of the other CPUs.
		// The +1 rounds up, otherwise integer division gives 0 when we have fewer tasks than CPUs.
		uint32_t per = c->nr_tasks / (sched_online - 1) + 1;

		// Soft affinity doesn't survive a takeover
		// Blocked tasks move too, since they would otherwise wake up on a CPU that no longer runs tasks
		sched_move_tasks(c, d, per, MOVE_SOFT_ALWAYS, true);
	}

	// TODO: I still need to figure out how to signal to hard affinity tasks that they are being taken over
	// We may not actually need a signal for this.
	// I think we could have a field for hard affinity tasks to specify what exactly they want to happen in a takeover
	// They could choose to stay and wait, destroy the task, move to a new CPU, etc.
	// We could potentially signal *after* they have moved, that way they can figure out if they need to update internal state or whatever.
	// Would probably be part of the TCB.

	// Options could be:
	//     - STAY (wait for a return to normal that may never happen)
	//     - MOVE (move to a new CPU, keeps affinity on that CPU, and gets a signal that it's been moved, so it can redetermine internal state)
	//     - DESTROY (kill the task, regularly, so it still cascade kills children)
	//     - DEMOTE (remove the hard affinity and add soft affinity on a new CPU)
}