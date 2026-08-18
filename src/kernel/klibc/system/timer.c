#include <system/timer.h>
#include <string.h>
#include <memory/kernel_alloc.h>


static counter_clock_t* counter_list = NULL;
static counter_clock_t* counter_best = NULL;
static interval_clock_t* interval_list = NULL;
static interval_clock_t* interval_best = NULL;
static wallclock_t* wallclock_list = NULL;
static wallclock_t* wallclock_best = NULL;

// In an attempt at having as much information about what we've initialized, we have /dev/timer/
// This lives for the life of the OS
static wallos_device_t* timer_root_dev = NULL;

// Internal storage for the total system uptime, in microseconds
static volatile uint64_t system_uptime_us = 0;

// ------------------------------------------------------------------------------------------------
// Cached clock for us/ns waits
// ------------------------------------------------------------------------------------------------

// This cache holds the pointer to the best clock, the read() function for that clock, 
// and computed ticks_per_X so we avoid division on every call.
// This makes time calculations a simple 64x64->128-bit multiply + shift
typedef struct {
	counter_clock_t* clock;
	uint64_t(*read)(counter_clock_t* self);
	uint64_t ticks_per_us_q32; /* (frequency_hz << 32) / 1,000,000 */
	uint64_t ticks_per_ns_q32; /* (frequency_hz << 32) / 1,000,000,000 */
} best_counter_cache_t;

#define TICK_SCALE_SHIFT 32

static best_counter_cache_t best_counter_cache = { 0 };

static void refresh_best_counter_cache(void) {
	if (!counter_best) {
		// idk how we got here, but if there's no best counter, we need to make sure the cache is zero
		best_counter_cache = (best_counter_cache_t){ 0 };
		return;
	}

	best_counter_cache.clock = counter_best;
	best_counter_cache.read = counter_best->read;

	best_counter_cache.ticks_per_us_q32 = (counter_best->frequency_hz << TICK_SCALE_SHIFT) / 1000000ULL;
	best_counter_cache.ticks_per_ns_q32 = (counter_best->frequency_hz << TICK_SCALE_SHIFT) / 1000000000ULL;
}


// ------------------------------------------------------------------------------------------------
// Counter Clock
// ------------------------------------------------------------------------------------------------

int counter_clock_register(counter_clock_t* clock) {
	if (!clock || !clock->read) return -1;

	clock->next = counter_list;
	counter_list = clock;

	if (!counter_best || clock->rating > counter_best->rating) {
		counter_best = clock;
		refresh_best_counter_cache();
	}

	return 0;
}

counter_clock_t* counter_clock_get_by_name(const char* name) {
	for (counter_clock_t* c = counter_list; c; c = c->next)
		if (strcmp(c->name, name) == 0) return c;
	return NULL;
}

counter_clock_t* counter_clock_get_best(void) { return counter_best; }
uint64_t counter_clock_read(counter_clock_t* clock) { return clock->read(clock); }
counter_clock_t* counter_clock_get_first(void) { return counter_list; }

// ------------------------------------------------------------------------------------------------
// Interval Clock
// ------------------------------------------------------------------------------------------------

int interval_clock_register(interval_clock_t* clock) {
	if (!clock) return -1;

	clock->next = interval_list;
	interval_list = clock;

	if (!interval_best || clock->rating > interval_best->rating)
		interval_best = clock;

	return 0;
}

interval_clock_t* interval_clock_get_by_name(const char* name) {
	for (interval_clock_t* c = interval_list; c; c = c->next)
		if (strcmp(c->name, name) == 0) return c;
	return NULL;
}

interval_clock_t* interval_clock_get_best(void) { return interval_best; }
interval_clock_t* interval_clock_get_first(void) { return interval_list; }

// ------------------------------------------------------------------------------------------------
// Wall Clock
// ------------------------------------------------------------------------------------------------

int wallclock_register(wallclock_t* clock) {
	if (!clock || !clock->read) return -1;

	clock->next = wallclock_list;
	wallclock_list = clock;

	if (!wallclock_best || clock->rating > wallclock_best->rating)
		wallclock_best = clock;

	return 0;
}

bool wallclock_read(wall_time_t* out) {
	if (!wallclock_best) return false;
	wallclock_best->read(wallclock_best, out);
	return true;
}

wallclock_t* wallclock_get_best(void) { return wallclock_best; }
wallclock_t* wallclock_get_first(void) { return wallclock_list; }

// ------------------------------------------------------------------------------------------------
// Busy Wait & Uptime
// ------------------------------------------------------------------------------------------------

void busy_wait_ns(uint32_t ns) {
	if (!best_counter_cache.clock) return;

	uint64_t ticks_needed = (uint64_t) (((__uint128_t) ns * best_counter_cache.ticks_per_ns_q32) >> TICK_SCALE_SHIFT);
	if (ticks_needed == 0) ticks_needed = 1; /* round short waits up to 1 tick */

	uint64_t(*read)(counter_clock_t*) = best_counter_cache.read;
	counter_clock_t* cs = best_counter_cache.clock;

	uint64_t start = read(cs);
	while ((read(cs) - start) < ticks_needed) { }
}

void busy_wait_us(uint32_t us) {
	if (!best_counter_cache.clock) return;

	uint64_t ticks_needed = (uint64_t) (((__uint128_t) us * best_counter_cache.ticks_per_us_q32) >> TICK_SCALE_SHIFT);
	if (ticks_needed == 0) ticks_needed = 1;  // round short waits up to 1 tick

	uint64_t(*read)(counter_clock_t*) = best_counter_cache.read;
	counter_clock_t* cs = best_counter_cache.clock;

	// Unsigned subtraction handles single wraparound correctly as long as read() is consistent about masking to counter_bits width
	uint64_t start = read(cs);
	while ((read(cs) - start) < ticks_needed) { }
}


void busy_wait_ms(uint32_t ms) { busy_wait_us(ms * 1000); }

void timer_tick_us(uint32_t us) { system_uptime_us += us; }
uint64_t timer_uptime_us(void) { return system_uptime_us; }
uint64_t timer_uptime_ms(void) { return system_uptime_us / 1000ULL; }

wallos_device_t* get_root_timer() {
	if (timer_root_dev == NULL) {
		timer_root_dev = (wallos_device_t*) kcalloc(1, sizeof(wallos_device_t));
		timer_root_dev->interfaces = DEV_INT_TIMER | DEV_INT_INTERFACE_ONLY;
		timer_root_dev->name = "timer";
		register_device(timer_root_dev);
	}
	return timer_root_dev;
}