#pragma once

#include <memory/kernel_alloc.h>
#include <scheduler/scheduler.h>

#include <string.h>

#include <wallos_attributes.h>

/* info builders */
#define TASK_NAMED(nm)                    ((task_create_info_t) {.name = (nm), .priority = SCHED_PRIO_INHERIT, .affinity = AFFINITY_INHERIT, .cpu = CPU_ANY})
#define TASK_INFO_PRIO(nm, prio)          ((task_create_info_t) {.name = (nm), .priority = (prio), .affinity = AFFINITY_INHERIT, .cpu = CPU_ANY})
#define TASK_INFO_PINNED(nm, cpu_, aff)   ((task_create_info_t) {.name = (nm), .priority = SCHED_PRIO_INHERIT, .affinity = (aff), .cpu = (cpu_)})
#define TASK_INFO_DETACHED(nm)            ((task_create_info_t) {.name = (nm), .priority = SCHED_PRIO_INHERIT, .affinity = AFFINITY_INHERIT, .cpu = CPU_ANY, .flags = TASK_CREATE_DETACHED})
#define TASK_INFO_KERNEL(nm, kfl, quanta) ((task_create_info_t) {.name = (nm), .priority = SCHED_PRIO_INHERIT, .affinity = AFFINITY_INHERIT, .cpu = CPU_ANY, .flags = TASK_CREATE_KERNEL, .kflags = (kfl), .kquanta = (quanta)})

/* Plain void fn(void*) with an info block. */
WALLOS_ALWAYS_INLINE task_id_t task_create_with(task_create_info_t info, task_entry_t fn, void* arg) {
	info.entry = fn;
	info.arg = arg;
	return task_create(&info);
}

/* Layout of the block for main-style functions
 * We only want one allocation.
 * This header, then the argv pointer array (argc + 1 entries, the last NULL), then the argument strings.
 */
typedef struct {
	int (*fn)(int, char**);
	int argc;
	char** argv;
} _task_main_pack_t;

WALLOS_ALWAYS_INLINE void _task_run_void(void* a) { ((void (*)(void)) a)(); }
WALLOS_ALWAYS_INLINE void _task_run_int(void* a) { (void) ((int (*)(void)) a)(); }
WALLOS_ALWAYS_INLINE void _task_run_main(void* a) {
	_task_main_pack_t* p = a;
	(void) p->fn(p->argc, p->argv);
}

WALLOS_ALWAYS_INLINE task_id_t task_spawn_void(task_create_info_t info, void (*fn)(void)) {
	if (!fn) return TASK_ID_INVALID;
	info.entry = _task_run_void;
	info.arg = (void*) fn; // the function pointer itself is the argument, nothing to free
	return task_create(&info);
}

WALLOS_ALWAYS_INLINE task_id_t task_spawn_int(task_create_info_t info, int (*fn)(void)) {
	if (!fn) return TASK_ID_INVALID;
	info.entry = _task_run_int;
	info.arg = (void*) fn;
	return task_create(&info);
}

WALLOS_ALWAYS_INLINE task_id_t task_spawn_main(task_create_info_t info, int (*fn)(int, char**), int argc, char** argv) {
	if (!fn || argc < 0 || (argc > 0 && !argv)) return TASK_ID_INVALID;
	if ((size_t) argc > TASK_ARGV_MAX_BYTES / sizeof(char*)) return TASK_ID_INVALID; // stops an overflow below

	// Some of this code is kind of cused because of how we have to handle the args

	// Work out the block size first. The strings are copied, so a NULL entry can't be handled.
	size_t bytes = sizeof(_task_main_pack_t) + ((size_t) argc + 1) * sizeof(char*);
	for (int i = 0; i < argc; i++) {
		if (!argv[i]) return TASK_ID_INVALID;
		bytes += strlen(argv[i]) + 1;
		if (bytes > TASK_ARGV_MAX_BYTES) return TASK_ID_INVALID;
	}

	_task_main_pack_t* p = kalloc(bytes);
	if (!p) return TASK_ID_INVALID;

	p->fn = fn;
	p->argc = argc;
	p->argv = (char**) (p + 1);

	// Strings go right after the pointer array
	char* s = (char*) &p->argv[argc + 1];
	for (int i = 0; i < argc; i++) {
		size_t n = strlen(argv[i]) + 1;
		memcpy(s, argv[i], n);
		p->argv[i] = s;
		s += n;
	}
	p->argv[argc] = NULL;

	info.entry = _task_run_main;
	info.arg = p;
	info.flags |= TASK_CREATE_OWN_ARG;

	task_id_t id = task_create(&info);
	if (!id) kfree(p); // creation failed, so the task never took ownership
	return id;
}