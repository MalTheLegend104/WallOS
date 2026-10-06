#include <string.h>

#include <memory/kernel_alloc.h>
#include <scheduler/sched_internal.h>

/* One lock protects the parent/child tree and the id registry.
 * Held only for short pointer "surgery" and IPI sends.
 * Never held at the same time as a rq lock.
 */
#define REG_BUCKETS 256
static sched_lock_t tree_lock;
static task_t* registry[REG_BUCKETS];
static volatile uint64_t next_task_id = 1;

static void copy_name(task_t* t, const char* name) {
	size_t i = 0;

	if (name) {
		while (name[i] && i < sizeof(t->name) - 1) {
			t->name[i] = name[i];
			i++;
		}
	}

	t->name[i] = '\0';
}

static void registry_insert(task_t* t) { /* tree_lock held */
	task_t** b = &registry[t->id % REG_BUCKETS];
	t->hash_next = *b;
	*b = t;
}
static void registry_remove(task_t* t) { /* tree_lock held */
	task_t** current = &registry[t->id % REG_BUCKETS];

	while (*current) {
		if (*current == t) {
			*current = t->hash_next;
			return;
		}

		current = &(*current)->hash_next;
	}
}
static task_t* registry_find(task_id_t id) { /* tree_lock held */
	// im sorry for this cursed for loop, it's easier this way
	for (task_t* t = registry[id % REG_BUCKETS]; t; t = t->hash_next) {
		if (t->id == id) {
			return t;
		}
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Creation
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static cpu_t* choose_cpu(uint32_t target, cpu_t* self) {
	if (target != CPU_ANY && target < sched_online) return &system_cpus[target];

	// Assignment never looks at load
	// idle CPU if one advertises, else creator's
	int32_t idle = sched_claim_idle_cpu(self->id);
	return idle >= 0 ? &system_cpus[idle] : self;
}

task_id_t task_create(const task_create_info_t* info) {
	if (!info || !info->entry) return TASK_ID_INVALID;
	if (info->priority != SCHED_PRIO_INHERIT && (info->priority < 0 || info->priority >= SCHED_PRIO_LEVELS)) return TASK_ID_INVALID;

	cpu_t* self = cpu_current();
	task_t* creator = self->current;
	if (creator == self->idle) creator = NULL;
	bool kernel = info->flags & TASK_CREATE_KERNEL;

	task_t* t = kalloc(sizeof *t);
	if (!t) return TASK_ID_INVALID;
	memset(t, 0, sizeof *t);

	size_t ssz = info->stack_size ? info->stack_size : SCHED_DEFAULT_STACK;
	void* stack = kalloc(ssz);
	if (!stack) {
		kfree(t);
		return TASK_ID_INVALID;
	}

	/* priority / affinity */
	int prio = info->priority;
	if (prio == SCHED_PRIO_INHERIT) prio = (creator && !creator->is_kernel) ? creator->priority : SCHED_PRIO_DEFAULT;

	int aff = info->affinity;
	uint32_t target = info->cpu;
	if (aff == AFFINITY_INHERIT) {
		aff = creator && !creator->is_kernel ? creator->affinity : AFFINITY_NONE;
		if (aff != AFFINITY_NONE && target == CPU_ANY) target = creator->cpu;
	}
	if (aff != AFFINITY_NONE && target == CPU_ANY) target = self->id;
	if (aff == AFFINITY_NONE) target = CPU_ANY;

	/* TCB */

	t->id = __atomic_fetch_add(&next_task_id, 1, __ATOMIC_RELAXED);
	t->stack_base = stack;
	t->stack_size = ssz;
	t->owns_stack = true;
	t->entry = info->entry;
	t->arg = info->arg;
	t->owns_arg = info->flags & TASK_CREATE_OWN_ARG;
	t->is_kernel = kernel;
	t->priority = kernel ? SCHED_PRIO_KERNEL : (uint8_t) prio;
	t->affinity = kernel ? AFFINITY_NONE : (uint8_t) aff;
	t->kflags = kernel ? info->kflags : 0;
	t->kquanta = info->kquanta;
	t->flags = (info->flags & TASK_CREATE_SUSPENDED) ? TASK_F_SUSPENDED : 0;
	copy_name(t, info->name);
	t->saved_ctx = arch_task_context_init(stack, ssz, info->entry, info->arg);

	task_id_t id = t->id;

	/* parentage & registry */
	uint64_t f = sched_lock_irqsave(&tree_lock);
	task_t* parent = NULL;
	if (creator && !(info->flags & TASK_CREATE_DETACHED))
		parent = (info->flags & TASK_CREATE_SIBLING) ? creator->parent : creator;
	t->parent = parent;
	if (parent) {
		t->sib_next = parent->child_head;
		if (parent->child_head) parent->child_head->sib_prev = t;
		parent->child_head = t;
	}
	registry_insert(t);
	sched_unlock_irqrestore(&tree_lock, f);

	/* placement */
	// t may be destroyed by another CPU the instant it is queued
	cpu_t* dst = choose_cpu(target, self);
	if (kernel && (t->kflags & KTASK_TAKEOVER)) __atomic_add_fetch(&dst->takeover, 1, __ATOMIC_ACQ_REL);
	uint32_t kfl = t->kflags;

	f = sched_lock_irqsave(&dst->rq_lock);
	rq_add_locked(dst, t, false);
	sched_unlock_irqrestore(&dst->rq_lock, f);

	if (kernel && (kfl & (KTASK_PREEMPT | KTASK_TAKEOVER))) arch_send_kpreempt_ipi(dst->id);
	else sched_kick(dst);

	return id;
}

task_id_t task_spawn(const char* name, task_entry_t entry, void* arg) {
	task_create_info_t i = TASK_INFO(name, entry, arg);
	return task_create(&i);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Destruction
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

void sched_task_destroy(task_t* t) {
	cpu_t* here = cpu_current();

	// Any wake racing with us from now on is discarded (task_wake -> false)
	uint32_t old = __atomic_fetch_or(&t->flags, TASK_F_DYING, __ATOMIC_ACQ_REL);
	if ((old & TASK_F_BLOCKED) && t->blocker_cancel) {
		t->blocker_cancel(t, t->blocker_ctx);
	}

	uint64_t f = sched_lock_irqsave(&tree_lock);
	registry_remove(t);
	// Unlink us from the sibling chain
	if (t->parent) {
		if (t->sib_prev) t->sib_prev->sib_next = t->sib_next;
		else t->parent->child_head = t->sib_next;

		if (t->sib_next) t->sib_next->sib_prev = t->sib_prev;
	}

	// We cascade on the child tasks.
	// Orphan every child and mark it pending-kill
	// The scheduler will take care of them when it comes across them.
	task_t* ch = t->child_head;
	while (ch) {
		task_t* nx = ch->sib_next;
		ch->parent = NULL;
		ch->sib_next = ch->sib_prev = NULL;
		__atomic_fetch_or(&ch->flags, TASK_F_PENDING_KILL, __ATOMIC_RELEASE);
		if (ch->cpu != here->id) arch_send_resched_ipi(ch->cpu);
		ch = nx;
	}
	t->child_head = NULL;
	sched_unlock_irqrestore(&tree_lock, f);

	if (t->is_kernel && (t->kflags & KTASK_TAKEOVER)) __atomic_sub_fetch(&system_cpus[t->cpu].takeover, 1, __ATOMIC_ACQ_REL);

	// We might be running on the task's own stack
	// We defer the actual destruction to the next scheduling pass
	t->rq_next = here->zombies;
	here->zombies = t;
}

void sched_destroy_chain(task_t* t) {
	while (t) {
		task_t* nx = t->rq_next;
		t->rq_next = NULL;
		sched_task_destroy(t);
		t = nx;
	}
}

void sched_reap_zombies(cpu_t* c) {
	task_t* t = c->zombies;
	c->zombies = NULL;
	while (t) {
		task_t* nx = t->rq_next;
		if (t->owns_stack) kfree(t->stack_base);
		kfree(t);
		t = nx;
	}
}

void task_exit(void) {
	task_t* t = cpu_current()->current;
	__atomic_fetch_or(&t->flags, TASK_F_PENDING_KILL, __ATOMIC_RELEASE);
	// forced yield that never returns. even if it somehow does, it will just infinitely yield.
	while (true) arch_yield();
}

bool task_kill(task_id_t id) {
	cpu_t* self = cpu_current();
	uint64_t f = sched_lock_irqsave(&tree_lock);
	task_t* t = registry_find(id);
	if (!t) {
		sched_unlock_irqrestore(&tree_lock, f);
		return false;
	}

	__atomic_fetch_or(&t->flags, TASK_F_PENDING_KILL, __ATOMIC_RELEASE);
	if (t->cpu != self->id) arch_send_resched_ipi(t->cpu); // t valid: destroy needs tree_lock
	sched_unlock_irqrestore(&tree_lock, f);
	return true;
}

bool task_suspend(task_id_t id) {
	uint64_t f = sched_lock_irqsave(&tree_lock);
	task_t* t = registry_find(id);
	if (t) __atomic_fetch_or(&t->flags, TASK_F_SUSPENDED, __ATOMIC_RELEASE);
	sched_unlock_irqrestore(&tree_lock, f);
	return t != NULL;
}

bool task_resume(task_id_t id) {
	uint64_t f = sched_lock_irqsave(&tree_lock);
	task_t* t = registry_find(id);
	uint32_t cpu = 0;
	if (t) {
		cpu = t->cpu;
		__atomic_fetch_and(&t->flags, ~TASK_F_SUSPENDED, __ATOMIC_RELEASE);
	}
	sched_unlock_irqrestore(&tree_lock, f);
	if (t) sched_kick(&system_cpus[cpu]);
	return t != NULL;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Blocking
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------


void task_block_begin(task_t* t, task_cancel_fn cancel, void* ctx) {
	t->blocker_cancel = cancel;
	t->blocker_ctx = ctx;
	__atomic_fetch_or(&t->flags, TASK_F_BLOCKED, __ATOMIC_RELEASE);
}

bool task_wake(task_t* t) {
	uint32_t cpu = t->cpu;
	uint32_t f = __atomic_load_n(&t->flags, __ATOMIC_ACQUIRE); // t may be destroyed
	do {
		if (f & TASK_F_DYING) return false;
		if (!(f & TASK_F_BLOCKED)) return true;
	} while (!__atomic_compare_exchange_n(&t->flags, &f, f & ~TASK_F_BLOCKED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
	sched_kick(&system_cpus[cpu]);
	return true;
}

bool task_wake_id(task_id_t id) {
	uint64_t f = sched_lock_irqsave(&tree_lock); // keeps the TCB alive during the wake
	task_t* t = registry_find(id);
	bool ok = t ? task_wake(t) : false;
	sched_unlock_irqrestore(&tree_lock, f);
	return ok;
}

void sched_block_wait(void) {
	task_t* t = cpu_current()->current;
	while (__atomic_load_n(&t->flags, __ATOMIC_ACQUIRE) & TASK_F_BLOCKED) arch_yield(); // scheduler skips us until the flag clears
}

void sched_block_current(task_cancel_fn cancel, void* ctx) {
	task_block_begin(cpu_current()->current, cancel, ctx);
	sched_block_wait();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Boot task adoption
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

task_t* sched_adopt_boot_task(const char* name) {
	cpu_t* c = cpu_current();
	task_t* t = kalloc(sizeof *t);
	if (!t) return NULL;
	memset(t, 0, sizeof *t);

	t->id = __atomic_fetch_add(&next_task_id, 1, __ATOMIC_RELAXED);
	t->priority = SCHED_PRIO_DEFAULT;
	t->affinity = AFFINITY_SOFT; // no reason bringup couldn't be completed on another CPU
	t->cpu = c->id;
	t->owns_stack = false;
	copy_name(t, name);

	uint64_t f = sched_lock_irqsave(&tree_lock);
	registry_insert(t);
	sched_unlock_irqrestore(&tree_lock, f);

	c->current = t;
	return t;
}