#include <stdint.h>
#include <stdlib.h>

#include <x86_64/lapic.h>

static volatile uint64_t* lapic_base = NULL;


static inline uint32_t lapic_read(uint32_t offset) {
	return *(volatile uint32_t*) ((uintptr_t) lapic_base + offset);
}

static inline void lapic_write(uint32_t offset, uint32_t value) {
	*(volatile uint32_t*) ((uintptr_t) lapic_base + offset) = value;

	// Ensure the write completes before returning
	// Reads from LAPIC registers are serializing, so do a dummy read
	(void) *(volatile uint32_t*) ((uintptr_t) lapic_base + offset);
}

void lapic_send_ipi(uint8_t apic_id, uint32_t icr_low) {
	while (lapic_read(0x300) & (1 << 12)); // wait until not busy

	lapic_write(0x310, apic_id << 24);
	lapic_write(0x300, icr_low);
}

void set_lapic_base(uint64_t* base) { lapic_base = base; }

/**
 * @brief Writes to the LAPIC SVR, masks the LVT entries, and sets the TPR to 0 (accept all interrupts)
 *
 */
void bsp_init_lapic() {
	// Enable LAPIC + set spurious vector
	lapic_write(LAPIC_SVR, SPURIOUS_VECTOR | (1 << 8));

	// Mask LVT entries (Timer, LINT0, LINT1, Error)
	lapic_write(LAPIC_LVT_TIMER, 1 << 16);
	lapic_write(LAPIC_LVT_LINT0, 1 << 16);
	lapic_write(LAPIC_LVT_LINT1, 1 << 16);
	lapic_write(LAPIC_LVT_ERROR, 1 << 16);

	// Task Priority Register = 0 (accept all interrupts)
	lapic_write(LAPIC_TPR, 0);

	// Clear any pending EOI
	lapic_write(LAPIC_EOI, 0);
}

void lapic_sleep_us(uint64_t lapic_freq, uint64_t us) {
	uint64_t effective_freq = lapic_freq / 16;
	uint64_t ticks = (effective_freq / 1000) * us / 1000;

	if (ticks == 0) return;
	if (ticks > 0xFFFFFFFF) ticks = 0xFFFFFFFF;	// clamp to 32-bit counter

	lapic_write(LAPIC_DIVIDE_CONFIG, 0x3);		// divide by 16
	lapic_write(LAPIC_LVT_TIMER, (1 << 16));	// masked, one-shot
	lapic_write(LAPIC_INITIAL_COUNT, (uint32_t) ticks);

	while (lapic_read(LAPIC_CURRENT_COUNT) != 0)
		__asm__ volatile("pause");
}

uint64_t calibrate_lapic_timer_with_tsc(uint64_t tsc_freq) {
	lapic_write(LAPIC_DIVIDE_CONFIG, 0x3);          // divide by 16
	lapic_write(LAPIC_LVT_TIMER, (1 << 16));        // masked, one-shot
	lapic_write(LAPIC_INITIAL_COUNT, 0xFFFFFFFF);

	uint64_t tsc_start = rdtsc_serialized();
	uint64_t wait_ticks = tsc_freq / 100;           // 10ms

	while ((rdtsc_serialized() - tsc_start) < wait_ticks)
		__asm__ volatile("pause");

	uint32_t remaining = lapic_read(LAPIC_CURRENT_COUNT);
	uint32_t elapsed = 0xFFFFFFFF - remaining;

	return (uint64_t) elapsed * 100 * 16;            // LAPIC bus Hz
}
