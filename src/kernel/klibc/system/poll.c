#include <stdbool.h>
#include <stdint.h>

#include <scheduler/scheduler.h>
#include <system/poll.h>
#include <system/timer.h>

#include <acpi/acpi_api.h>
#include <drivers/usb/class/hid/hid_common.h>

#define SYSTEM_POLL_INTERVAL_US 2000ull /* upper bound on polling latency */
#define SYSTEM_POLL_PRIORITY    1
#define SYSTEM_POLL_CPU         0 /* BSP (logical id 0) */

static volatile int poll_busy;
static volatile task_id_t poll_task_id;
static timer_callback_t poll_timer;

static void system_poll_raw() {
	hid_keyboard_poll_all();
	acpi_poll_events();
}

void system_poll_once(void) {
	// In a halted state (like a panic) we still need to poll for events from some subsystems.
	// I will probably change this later to designate "critical" (ACPI) polling subsystems from "regular" (USB) ones.\
	// In a halted state, there's a chance that more than one CPU might try to call this.
	//
	// We try once, skip on contention
	if (__atomic_test_and_set(&poll_busy, __ATOMIC_ACQUIRE)) return;
	system_poll_raw();
	__atomic_clear(&poll_busy, __ATOMIC_RELEASE);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// The actual task
// This task needs to be handled different to mostly everything else.
// It's a P1 task so it runs immediately after whatever task is currently running.
// Worst case this ends up being up to 128ms, which is a lot, but nothing should really be running in that priority level anyway.
// Normal case is at most 16ms, which is fine.
// We set it up as a "self blocking" task, it will run, set itself as blocked, and unblocks itself based on a PIT timer callback every 2ms.
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// actual task loop
// only way to stop it is to destroy it
static void system_poll_task(void* unused) {
	(void) unused;
	for (;;) {
		system_poll_once();
		sched_block_current(NULL, NULL); /* until poll_timer_tick() wakes us */
	}
}

// This is the PIT timer callback
static void poll_timer_tick(timer_callback_t* cb, void* ctx) {
	(void) cb;
	(void) ctx;
	task_wake_id(poll_task_id);
}

void system_poll_start(void) {
	if (poll_task_id) return;

	task_create_info_t i = TASK_INFO("syspoll", system_poll_task, NULL);
	i.priority = SYSTEM_POLL_PRIORITY;
	i.affinity = AFFINITY_SOFT;
	i.cpu = SYSTEM_POLL_CPU;
	i.flags = TASK_CREATE_DETACHED;
	poll_task_id = task_create(&i);

	poll_timer.callback_fn = poll_timer_tick;
	poll_timer.ctx = NULL;
	timer_register_callback(&poll_timer, SYSTEM_POLL_INTERVAL_US, true);
}