#ifndef TIMING_H
#define TIMING_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// CMOS Defines go here for now
// This will (probably) be its own header eventually.
#define CMOS_OUT_PORT 0x70
#define CMOS_IN_PORT  0x71
#define CMOS_SECONDS  0x00
#define CMOS_MINUTES  0x02
#define CMOS_HOURS    0x04
#define CMOS_WEEKDAY  0x06
#define CMOS_DAY      0x07
#define CMOS_MONTH    0x08
#define CMOS_YEAR     0x09
#define CMOS_CENTURY  0x32
#define CMOS_STATUS_A 0x0A
#define CMOS_STATUS_B 0x0B

	typedef long double time_t;

	void sleep(size_t ms);
	size_t get_system_up_time();
	void incriment_sys_time();
	// Function to initialize the PIT and set up the desired interrupt frequency
	void pit_init(uint16_t frequency_ms);

#ifdef __cplusplus
}
#endif
#endif