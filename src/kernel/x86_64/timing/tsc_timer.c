#include <x86_64/tsc.h>
#include <stdint.h>

// Cache the frequency in MHz to make the math fast
static uint64_t g_tsc_freq_mhz = 0;

void timer_no_interrupts_init(void) {
	uint64_t tsc_freq_hz = get_tsc_freq();

	// Convert to MHz
	g_tsc_freq_mhz = tsc_freq_hz / 1000000;

	if (g_tsc_freq_mhz == 0) {
		g_tsc_freq_mhz = 1; // Fallback to prevent div-by-zero
	}
}

uint64_t timer_uptime_no_interrupts(void) {
	if (g_tsc_freq_mhz == 0) return 0;

	uint64_t ticks = rdtsc_serialized();

	// We want: nanoseconds = (ticks * 1000) / g_tsc_freq_mhz
	// To prevent 64-bit overflow on long uptimes, we split the division:
	// ns = (ticks / MHz) * 1000 + ((ticks % MHz) * 1000) / MHz

	uint64_t base_ns = (ticks / g_tsc_freq_mhz) * 1000;
	uint64_t remainder_ns = ((ticks % g_tsc_freq_mhz) * 1000) / g_tsc_freq_mhz;

	return base_ns + remainder_ns;
}