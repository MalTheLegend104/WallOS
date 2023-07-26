#ifndef TIMING_H
#define TIMING_h
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
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