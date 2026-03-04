#ifndef WALLOS_NEW_INTERFACE_H
#define WALLOS_NEW_INTERFACE_H

#include <stdint.h>
#include <memory/spinlock.h>

typedef uint64_t pid_t;

// Forward declare, this should be implementation defined.
struct arch_context;

typedef struct cpu_t;

typedef enum {
	TASK_RUNNING,
	TASK_READY,
	TASK_BLOCKED,
	TASK_ZOMBIE
} task_state_t;

/* Allows quick differentiation between user and kernel tasks.
 * This *does not* mean it's a priority kernel task. Those are handled *very* differently.
 */
typedef enum {
	TASK_KERNEL,
	TASK_USER
} task_type_t;

// This is only for containing 
typedef struct task_descriptor {
	pid_t pid;
	task_type_t type;

	// Array of child tasks, not LL
	struct task_descriptor* child_task;
	uint64_t child_task_count;

	struct arch_context_t* context;

	// A value of UINT16 MAX indicates *no affinity*
	// Otherwise it's the CPU number
	// Internally WallOS simply assigns each CPU an id, starting at 0 (the BSP)
	uint16_t affinity_cpu;
	uint8_t affinity_level; // To distinguish None (0), Soft (1), Hard (2)
	uint8_t priority;       // 0-9

	// We need the doubly linked list so we can remove things from the tail of the queue.
	// In contexts where we need the task but not the list, this doesn't really matter if it's present or not.
	struct task_descriptor* next;
	struct task_descriptor* prev;
} task_t;

typedef struct {
	struct task_t* head;
	struct task_t* tail;
	uint64_t count;


	uint8_t  consecutive_limit; // Calculated 'back_to_back'
	uint8_t  consecutive_count; // how far into the back-to-back we are
} runqueue_t;

typedef struct {
	// 0-1 are overriding, 2-9 are standard
	runqueue_t 	runqueues[10];

	task_t* current;
	task_t* idle_task; // the base idle task

	// Bitmask: bit N is set if queues[N].count > 0
	uint16_t 	active_mask;
	// For D calculation: bit N is set if queues[N].count >= 20
	uint16_t 	saturated_mask;


	uint8_t 	current_level; // The priority level currently being serviced
	uint16_t 	cpu_id; // Which logical unit owns this rq

	// Other CPUs can look at this to see if we want to be "donated" threads.
	_Atomic uint8_t is_targetable;

	// Cooldown until we can try to steal tasks again.
	uint64_t 	steal_cooldown_ticks;

	// Total task count across all P0-P9 for quick comparisons
	// Use atomic increments/decrements when tasks move between CPUs
	/*atomic*/ uint64_t 	total_runnable_count;

	// Last time a full rebalance was attempted (for the "periodic" rule)
	uint64_t 	last_rebalance_ticks;

	// Protection for the lists themselves
	// Note: Stealing/Donating REQUIRES holding the owner's rq_lock
	spinlock_t 	rq_lock;
} cpu_runqueue_t;

typedef void (*task_entry_t)(void);
// Flags will be things like has_affinity, user/kernel, level, etc.
task_t* task_create(task_entry_t* entry_point, uint64_t flags);

/**
 * @brief Get the handle the CURRENT cpu
 *
 * @return cpu_t* handle to the CPU
 */
cpu_t* cpu_current(void);

#endif //WALLOS_NEW_INTERFACE_H