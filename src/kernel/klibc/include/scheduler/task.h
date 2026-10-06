#ifndef WALLOS_TASK_H
#define WALLOS_TASK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct cpu cpu_t;
	typedef struct task task_t;

	typedef uint64_t task_id_t;
#define TASK_ID_INVALID 0ull

	typedef void (*task_entry_t)(void* arg);
	typedef void (*task_cancel_fn)(task_t* t, void* ctx);

/* Priority */
#define SCHED_PRIO_LEVELS  10   /* 0 (highest) .. 9 (lowest) */
#define SCHED_PRIO_DEFAULT 5
#define SCHED_PRIO_KERNEL  255
#define SCHED_PRIO_INHERIT (-1)

	/* Affinity */
	typedef enum {
		AFFINITY_NONE = 0,
		AFFINITY_SOFT = 1,
		AFFINITY_HARD = 2,
	} affinity_t;
#define AFFINITY_INHERIT (-1)
#define CPU_ANY          0xFFFFFFFFu

/* Atomic per-task flags (TCB.flags) */
#define TASK_F_BLOCKED      (1u << 0)  /* set/cleared only by the blocking subsystem     */
#define TASK_F_SUSPENDED    (1u << 1)  /* created non-ready / explicitly suspended       */
#define TASK_F_PENDING_KILL (1u << 2)  /* destroy at next quanta boundary                */
#define TASK_F_DYING        (1u << 3)  /* destruction has begun. wakes are discarded     */

/* task_create_info.flags */
#define TASK_CREATE_SUSPENDED (1u << 0) /* do not schedule until task_resume()           */
#define TASK_CREATE_SIBLING   (1u << 1) /* parent = creator's parent                     */
#define TASK_CREATE_DETACHED  (1u << 2) /* no parent; exempt from cascading destruction  */
#define TASK_CREATE_KERNEL    (1u << 3) /* kernel-priority task (kq); see kflags         */
#define TASK_CREATE_OWN_ARG   (1u << 4) /* arg was kalloc'd: the scheduler kfree()s it when the task is destroyed */

/* kernel task behaviour flags (task_create_info.kflags) */
#define KTASK_PREEMPT           (1u << 0) /* preempt whatever is running, right now      */
#define KTASK_INTERLEAVE        (1u << 1) /* run in P0 slots between task transitions    */
#define KTASK_TAKEOVER          (1u << 2) /* take the CPU over. others are redistributed */
#define KTASK_RUN_TO_COMPLETION (1u << 3) /* no quanta timer while it runs               */
/* default (no flag): runs in the next timeslice after the current task ends             */

	typedef struct task_create_info {
		const char* name;
		task_entry_t entry;
		void* arg;
		int priority;        /* SCHED_PRIO_INHERIT or 0..9                        */
		int affinity;        /* AFFINITY_INHERIT or affinity_t                    */
		uint32_t cpu;        /* affinity target, CPU_ANY = creator's / inherited  */
		uint32_t flags;      /* TASK_CREATE_*                                     */
		uint32_t kflags;     /* KTASK_* (kernel tasks only)                       */
		uint32_t kquanta;    /* kernel tasks: quanta count, 1..64 (0 = 1)         */
		size_t stack_size;   /* 0 = SCHED_DEFAULT_STACK                           */
	} task_create_info_t;

	/* Starting point for task_create().
	 * Inherits priority and affinity from the creator (children of a soft-pinned task are pinned too), no CPU preference, default stack.
	 * Override fields on the result before creating.
	 */
#define TASK_INFO(nm, fn, a) \
	(task_create_info_t) { \
		.name = (nm), .entry = (fn), .arg = (a), \
		.priority = SCHED_PRIO_INHERIT, .affinity = AFFINITY_INHERIT, .cpu = CPU_ANY \
	}

	struct task {
		// TODO: Task id maybe shouldn't be strictly unique.
		// Might rework this a little, to be more in line with linux or BSDs.
		// I like the idea of having some "reserved" IDs for some things, like the poll loop always being ID 1.
		// I also think randomized IDs, and reused (or recycled like linux) isn't a horrible idea.
		task_id_t id; //< Unique ID, never reused. 0 (TASK_ID_INVALID) is invalid.
		void* saved_ctx; //< Opaque context handle owned by the arch port.

		void* stack_base; //< Lowest address of the task's stack allocation.
		size_t stack_size; //< Size of the stack in bytes.
		bool owns_stack; //< True if the scheduler allocated the stack and must free it. Some tasks can inherit an old tasks stack

		volatile uint32_t flags; //< Atomic TASK_F_* bits. Always use __atomic_* on it, other CPUs read and set it.
		volatile uint32_t cpu; //< Owning CPU (logical id). Atomic. Can only changed when queued (including when moved), never while being ran.

		uint8_t priority; //< 0-9 for user tasks, SCHED_PRIO_KERNEL (255) for kernel tasks.
		uint8_t affinity; //< An affinity_t: AFFINITY_NONE, AFFINITY_SOFT or AFFINITY_HARD.
		bool is_kernel; //< Kernel task. Lives in the CPU's kernel queue (kq), ignores the priority rules, never moved by balancing, not counted in load.
		uint32_t kflags; //< KTASK_* behavior flags (preempt, interleave, takeover, run to completion). Kernel tasks only, 0 otherwise.
		uint32_t kquanta; //< Quanta a kernel task asks for, 1 to SCHED_KERNEL_MAX_QUANTA. 0 is treated as 1.
		uint32_t remaining_us; //< Quanta left when a kernel preemption displaced the task. Used once on resume, then cleared. 0 means a full quanta.

		task_entry_t entry; //< Function the task starts in. Returning from it is a normal exit.
		void* arg; //< Argument passed to entry.
		bool owns_arg; //< arg is freed with the task (TASK_CREATE_OWN_ARG). The scheduler frees it, never the task.

		// Runqueue / zombie / reap-chain linkage (owner CPU, under rq_lock)
		task_t* rq_next; //< Next task in the runqueue. Reused as the link for the reap chain and the zombie list once the task is off every queue.
		task_t* rq_prev; //< Previous task in the runqueue. Only meaningful while queued.


		// Parent/child tree (global tree lock)
		task_t* parent; //< Parent of this task. NULL if detached, or if the parent was destroyed first
		task_t* child_head; //< Head of this task's list of children. Destroying the task cascades to all of them
		task_t* sib_next; //< Next child of the same parent
		task_t* sib_prev; //< Previous child of the same parent
		task_t* hash_next; //< Next task in the same id registry bucket (tree lock)


		// Blocking subsystem's cancel hook
		task_cancel_fn blocker_cancel; //< Called by destruction if the task was blocked. Subsystem must drop the reference to this task. Only valid while BLOCKED, never cleared after a wake.
		void* blocker_ctx; //< Passed back to blocker_cancel unchanged.

		char name[24]; //< Debug name, truncated to fit and always NUL terminated
	};

#ifdef __cplusplus
}
#endif
#endif // WALLOS_TASK_H