#ifndef WALLOS_LAPIC_H
#define WALLOS_LAPIC_H

#include <stdint.h>
#include <system/cpuid.h>
#include <stdbool.h>

#include <cpu_io.h>
// ID & Version
#define LAPIC_ID				0x020
#define LAPIC_VERSION			0x030

// Task / Priority
#define LAPIC_TPR				0x080
#define LAPIC_APR				0x090
#define LAPIC_PPR				0x0A0
#define LAPIC_EOI				0x0B0
#define LAPIC_RRD				0x0C0
#define LAPIC_LDR				0x0D0
#define LAPIC_DFR				0x0E0
#define LAPIC_SVR				0x0F0

// In-Service Registers (ISR)
#define LAPIC_ISR0				0x100
#define LAPIC_ISR1				0x110
#define LAPIC_ISR2				0x120
#define LAPIC_ISR3				0x130
#define LAPIC_ISR4				0x140
#define LAPIC_ISR5				0x150
#define LAPIC_ISR6				0x160
#define LAPIC_ISR7				0x170

// Trigger Mode Registers (TMR)
#define LAPIC_TMR0				0x180
#define LAPIC_TMR1				0x190
#define LAPIC_TMR2				0x1A0
#define LAPIC_TMR3				0x1B0
#define LAPIC_TMR4				0x1C0
#define LAPIC_TMR5				0x1D0
#define LAPIC_TMR6				0x1E0
#define LAPIC_TMR7				0x1F0

// Interrupt Request Registers (IRR)
#define LAPIC_IRR0				0x200
#define LAPIC_IRR1				0x210
#define LAPIC_IRR2				0x220
#define LAPIC_IRR3				0x230
#define LAPIC_IRR4				0x240
#define LAPIC_IRR5				0x250
#define LAPIC_IRR6				0x260
#define LAPIC_IRR7				0x270

// Error Status
#define LAPIC_ESR				0x280

// Interrupt Command Register (ICR)
#define LAPIC_ICR_LOW			0x300
#define LAPIC_ICR_HIGH			0x310

// Local Vector Table (LVT)
#define LAPIC_LVT_TIMER			0x320
#define LAPIC_LVT_THERMAL		0x330
#define LAPIC_LVT_PERF			0x340
#define LAPIC_LVT_LINT0			0x350
#define LAPIC_LVT_LINT1			0x360
#define LAPIC_LVT_ERROR			0x370

// Timer Registers
#define LAPIC_INITIAL_COUNT		0x380
#define LAPIC_CURRENT_COUNT		0x390
#define LAPIC_DIVIDE_CONFIG		0x3E0

// Spurious Interrupt Vector
#define SPURIOUS_VECTOR			0xFF

static inline uint32_t lapic_read(uint32_t offset);
static inline void lapic_write(uint32_t offset, uint32_t value);

void lapic_send_ipi(uint8_t apic_id, uint32_t icr_low);

void set_lapic_base(uint64_t* base);

void bsp_init_lapic();
void ap_init_lapic();

static inline uint64_t rdtsc_serialized(void) {
	uint32_t lo, hi;
	__asm__ volatile("lfence");
	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
	return ((uint64_t) hi << 32) | lo;
}

bool check_tsc_support() {
	uint32_t eax, ebx, ecx, edx;

	// Check for TSC (Feature Bit 4 of EDX for CPUID leaf 1)
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
		if (!(edx & (1 << 4))) return false;
	}

	// Check for Invariant TSC (Bit 8 of EDX for CPUID leaf 0x80000007)
	// This ensures TSC runs at constant freq regardless of P-states/C-states
	__get_cpuid(0x80000007, &eax, &ebx, &ecx, &edx);
	return (edx & (1 << 8));
}

uint64_t get_tsc_freq_cpuid() {
	uint32_t eax, ebx, ecx, edx;
	__get_cpuid(0x15, &eax, &ebx, &ecx, &edx);

	if (eax == 0 || ebx == 0) return 0; // Not supported on this CPU

	uint64_t crystal_hz = ecx;
	if (crystal_hz == 0) {
		// Many CPUs don't populate ECX. Use common defaults:
		// These are standard for many Intel platforms.
		crystal_hz = 24000000; // 24MHz
	}

	return (crystal_hz * ebx) / eax;
}

void pit_prepare_sleep(uint32_t ticks) {
	// Channel 2 (speaker), Mode 0 (interrupt on terminal count)
	// We use Channel 2 because we can read its status easily via Port 0x61
	uint8_t val = (inb(0x61) & 0xFD) | 1;
	outb(0x61, val);
	outb(0x43, 0xB2);
	outb(0x42, (uint8_t) ticks);
	outb(0x42, (uint8_t) (ticks >> 8));
}

uint64_t calibrate_tsc_with_pit() {
	// PIT Freq is 1193182, This should be moved to timing.h where PIT_FREQ is actually defined.
	uint32_t pit_ticks = 1193182 / 100; // 10ms window
	pit_prepare_sleep(pit_ticks);

	uint64_t tsc_start = rdtsc_serialized();

	// Wait for PIT to finish (Bit 5 of Port 0x61 goes high)
	while (!(inb(0x61) & 0x20));

	uint64_t tsc_end = rdtsc_serialized();
	return (tsc_end - tsc_start) * 100;
}

uint64_t get_tsc_freq() {
	if (!check_tsc_support()) return 0;

	// Method 1: CPUID leaf 0x15
	uint64_t freq = get_tsc_freq_cpuid();

	// Method 2: Manual Calibration (if CPUID failed)
	if (freq == 0) {
		freq = calibrate_tsc_with_pit();
	}

	return freq;
}

uint64_t calibrate_lapic_timer_with_tsc(uint64_t tsc_freq);

void lapic_sleep_us(uint64_t lapic_freq, uint64_t us);

extern void enable_lapic_msr(uint64_t lapic_phys);

#endif // WALLOS_LAPIC_H