#ifndef WALLOS_SCHEDULER_INTERNAL_H
#define WALLOS_SCHEDULER_INTERNAL_H
#include <scheduler/scheduler.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	extern uint32_t sched_online;
	extern volatile bool sched_ready;

	/* locks: test-and-test-and-set, IRQ-saving */
	static inline uint64_t sched_lock_irqsave(sched_lock_t* l) {
		uint64_t f = arch_irq_save();
		while (true) {
			if (!__atomic_exchange_n(&l->v, 1, __ATOMIC_ACQUIRE)) return f;
			while (__atomic_load_n(&l->v, __ATOMIC_RELAXED)) arch_cpu_relax();
		}
	}
	static inline bool sched_trylock_irqsave(sched_lock_t* l, uint64_t* f) {
		*f = arch_irq_save();
		if (!__atomic_exchange_n(&l->v, 1, __ATOMIC_ACQUIRE)) return true;
		arch_irq_restore(*f);
		return false;
	}
	static inline void sched_unlock_irqrestore(sched_lock_t* l, uint64_t f) {
		__atomic_store_n(&l->v, 0, __ATOMIC_RELEASE);
		arch_irq_restore(f);
	}

	/* intrusive lists  */
	static inline void list_push_back(task_list_t* l, task_t* t) {
		t->rq_next = NULL;
		t->rq_prev = l->tail;
		if (l->tail) l->tail->rq_next = t;
		else l->head = t;
		l->tail = t;
		l->len++;
	}
	static inline void list_push_front(task_list_t* l, task_t* t) {
		t->rq_prev = NULL;
		t->rq_next = l->head;
		if (l->head) l->head->rq_prev = t;
		else l->tail = t;
		l->head = t;
		l->len++;
	}
	static inline void list_unlink(task_list_t* l, task_t* t) {
		if (t->rq_prev) t->rq_prev->rq_next = t->rq_next;
		else l->head = t->rq_next;
		if (t->rq_next) t->rq_next->rq_prev = t->rq_prev;
		else l->tail = t->rq_prev;
		t->rq_next = t->rq_prev = NULL;
		l->len--;
	}

	/* Add/remove with counter upkeep. rq_lock of `c` must be held. */
	static inline void rq_add_locked(cpu_t* c, task_t* t, bool front) {
		__atomic_store_n(&t->cpu, c->id, __ATOMIC_RELAXED);
		task_list_t* l = t->is_kernel ? &c->kq : &c->rq[t->priority];
		if (front) list_push_front(l, t);
		else list_push_back(l, t);
		if (!t->is_kernel) {
			__atomic_fetch_add(&c->nr_tasks, 1, __ATOMIC_RELAXED);
			if (t->affinity == AFFINITY_NONE) __atomic_fetch_add(&c->nr_unpinned, 1, __ATOMIC_RELAXED);
		}
	}
	static inline void rq_del_locked(cpu_t* c, task_t* t) {
		task_list_t* l = t->is_kernel ? &c->kq : &c->rq[t->priority];
		list_unlink(l, t);
		if (!t->is_kernel) {
			__atomic_fetch_sub(&c->nr_tasks, 1, __ATOMIC_RELAXED);
			if (t->affinity == AFFINITY_NONE) __atomic_fetch_sub(&c->nr_unpinned, 1, __ATOMIC_RELAXED);
		}
	}

	/* Approximate load = queued user tasks + the one running (racy by design). */
	static inline uint32_t cpu_load(const cpu_t* c) {
		return c->nr_tasks + ((c->current && c->current != c->idle && !c->current->is_kernel) ? 1u : 0u) + (c->preempted ? 1u : 0u);
	}

	/* scheduler.c */
	void sched_kick(cpu_t* target); /* wake it if idle */

	/* balance.c */
	enum {
		MOVE_SOFT_NEVER,
		MOVE_SOFT_IF_COMPANY,
		MOVE_SOFT_ALWAYS
	};
	void sched_set_targetable(cpu_t* c);
	void sched_clear_targetable(cpu_t* c);
	int32_t sched_claim_idle_cpu(uint32_t exclude); /* atomically claims. -1 if none  */
	uint32_t sched_move_tasks(cpu_t* from, cpu_t* to, uint32_t max, int soft, bool blocked_ok);
	uint32_t sched_steal(cpu_t* thief);
	bool sched_idle_try_steal(cpu_t* c, uint64_t now);
	void sched_balance_periodic(cpu_t* me);
	void sched_redistribute(cpu_t* c); /* CPU takeover */

	/* task.c */
	void sched_task_destroy(task_t* t);
	void sched_destroy_chain(task_t* head); /* linked through rq_next */
	void sched_reap_zombies(cpu_t* c);

#ifdef __cplusplus
}
#endif
#endif // WALLOS_SCHEDULER_INTERNAL_H