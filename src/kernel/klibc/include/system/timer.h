#ifndef SYSTEM_TIMER_H
#define SYSTEM_TIMER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

	/* This file is intended to be as platform agnostic as possible. */

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Counter Clock
	// 
	// Used for calibrated busy waiting. Multiple can be registered at once, counter_clock_get_best()
	// will automatically use the highest resolution available. 
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	typedef struct counter_clock {
		const char* name;
		uint32_t    rating; // higher = preferred/more precise. there's not really a standard for this, there probably should be.
		uint64_t    frequency_hz;
		uint8_t     counter_bits; // width of the raw counter, for wraparound math

		uint64_t(*read)(struct counter_clock* self);

		void* priv;
		struct counter_clock* next; // we store a linked list of these internally
	} counter_clock_t;

	int counter_clock_register(counter_clock_t* clock);
	counter_clock_t* counter_clock_get_best(void);
	counter_clock_t* counter_clock_get_by_name(const char* name);
	uint64_t counter_clock_read(counter_clock_t* clock);

	// Not really meant for use, mostly for debug/diag. use counter_clock_get_best()
	counter_clock_t* counter_clock_get_first(void);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Interval Clock
	// 
	// A programmable device that raises an interrupt after N ticks, periodic or one-shot.
	// Mostly used for system timekeeping at 1ms interval. 
	// Other things can "piggyback" on this for 1ms resolution timing.
	// I may eventually move TSC timing here for scheduling, undecided rn
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	typedef enum interval_clock_mode {
		INTERVAL_CLOCK_SHUTDOWN = 0,
		INTERVAL_CLOCK_PERIODIC,
		INTERVAL_CLOCK_ONESHOT,
	} interval_clock_mode_t;

	typedef struct interval_clock {
		const char* name;
		uint32_t    rating;
		uint64_t    frequency_hz;

		void (*set_mode)(struct interval_clock* self, interval_clock_mode_t mode); // This isn't particularly required. PIT for example is purely periodic
		void (*set_next_event)(struct interval_clock* self, uint64_t ticks);

		void (*event_handler)(struct interval_clock* self);

		void* priv;
		struct interval_clock* next;
	} interval_clock_t;

	int interval_clock_register(interval_clock_t* clock);
	interval_clock_t* interval_clock_get_by_name(const char* name);

	interval_clock_t* interval_clock_get_best(void);
	interval_clock_t* interval_clock_get_first(void);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Wall Clock
	// 
	// An attempt at real world time.
	// This is initially just the CMOS clock, may eventually end up with NTP.
	// I just wanted this interface to match the others tbh (and makes porting to ARM or something easier)
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	typedef struct wall_time {
		uint16_t year;
		uint8_t  month;
		uint8_t  day;
		uint8_t  hour;
		uint8_t  minute;
		uint8_t  second;
	} wall_time_t;

	typedef struct wallclock {
		const char* name;
		uint32_t    rating;

		void (*read)(struct wallclock* self, wall_time_t* out);
		void (*write)(struct wallclock* self, const wall_time_t* in); // NULL if read-only

		void* priv;
		struct wallclock* next;
	} wallclock_t;

	int wallclock_register(wallclock_t* clock);
	bool wallclock_read(wall_time_t* out);
	wallclock_t* wallclock_get_best(void);
	wallclock_t* wallclock_get_first(void);

	/* Busy-wait spins on the best available counter_clock.
	 * Safe to call with interrupts disabled.
	 *
	 * There is a very crude, non-calibrated, io_delay that uses outb(0x80,0) to generate a small delay if there are no other registered clocks.
	 * Nanoseconds is a best effort timer, not guaranteed to actually be accurate. Assume the wait to be AT LEAST the specified time.
	 */
	void busy_wait_ns(uint32_t ns);
	void busy_wait_us(uint32_t us);
	void busy_wait_ms(uint32_t ms);

	// Uptime in ms, derived from the best registered counter_clock.
	uint64_t timer_uptime_ms(void);

	// Generic timekeeping update.
	// It is up to the ISA implementation to decide the source of the system time upkeep.
	// On x86_64 this is the PIT. 
	// Call this function from the timekeeping interrupt
	void timer_tick_us(uint32_t us);


#include <device/device_manager.h>
	wallos_device_t* get_root_timer();

#ifdef __cplusplus
}
#endif
#endif // SYSTEM_TIMER_H