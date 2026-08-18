#ifndef X86_64_TIMER_H
#define X86_64_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
	void pit_init(uint32_t frequency_hz);
	void pit_init_dev();
	// This is called from the PIT IRQ
	void pit_handle_tick(void);

	void rtc_cmos_init(void);

	// HPET is only used for busy waiting, not timekeeping.
	bool hpet_init(void);

	// A VERY crude counter_clock backed by writes to port 0x80
	// Usually takes about a microsecond. Could be more, could be less, could be instant.
	// Best effort, any other timer will supersede this
	void io_delay_init(void);

#ifdef __cplusplus
}
#endif
#endif // X86_64_TIMER_H