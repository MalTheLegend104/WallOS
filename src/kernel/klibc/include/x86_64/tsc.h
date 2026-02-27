#ifndef WALLOS_TSC_H
#define WALLOS_TSC_H

#include <stdbool.h>
#include <stdio.h>
#include <drivers/serial.h>
#include <system/cpuid.h>
#include <cpu_io.h>

#ifdef __cplusplus
extern "C" {
#endif

	static inline uint64_t rdtsc_serialized(void) {
		uint32_t lo, hi;
		__asm__ volatile("lfence");
		__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
		return ((uint64_t) hi << 32) | lo;
	}

	typedef struct {
		bool supported;
		bool invariant;
	} tsc_info_t;

	static tsc_info_t check_tsc_support() {
		tsc_info_t info = { .supported = false, .invariant = false };
		uint32_t eax, ebx, ecx, edx;

		// Check for basic TSC support (CPUID 1, EDX bit 4)
		if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
			if (edx & (1 << 4)) {
				info.supported = true;
			}
		}

		// If no TSC at all, we can't check for Invariant
		if (!info.supported) return info;

		// Check for Invariant TSC (CPUID 0x80000007, EDX bit 8)
		// First, check if the leaf itself is supported
		__get_cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
		if (eax >= 0x80000007) {
			__get_cpuid(0x80000007, &eax, &ebx, &ecx, &edx);
			if (edx & (1 << 8)) {
				info.invariant = true;
			}
		}

		return info;
	}

	static uint64_t get_tsc_freq_cpuid() {
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

	static uint64_t calibrate_tsc_with_pit() {
		// Set PIT Channel 0 to Mode 0 (Interrupt on Terminal Count)
		// We won't actually trigger an interrupt (CLI is on), 
		// we just want it to count down.
		uint16_t count = 0xFFFF; // Max count (~54.9ms)

		outb(0x43, 0x30); // Channel 0, lobyte/hibyte, mode 0
		outb(0x40, (uint8_t) count);
		outb(0x40, (uint8_t) (count >> 8));

		uint64_t tsc_start = rdtsc_serialized();

		// Poll the PIT until it hits a target. 
		// Since it counts down, we wait for it to drop by a certain amount.
		// 11932 ticks = ~10ms
		uint16_t current_count = 0;
		do {
			outb(0x43, 0x00); // Latch count
			uint8_t lo = inb(0x40);
			uint8_t hi = inb(0x40);
			current_count = (hi << 8) | lo;
		} while (current_count > (0xFFFF - 11932));

		uint64_t tsc_end = rdtsc_serialized();

		return (tsc_end - tsc_start) * 100; // 10ms * 100 = 1s
	}

	static uint64_t get_tsc_freq() {
		tsc_info_t tsc = check_tsc_support();

		// This is honestly just debug information right now, but it *is* very important for us later on.
		if (!tsc.supported) {
			printf_color(PRINT_COLOR_RED, PRINT_DEFAULT_BG, "TSC not supported by CPU!\n");
			return 0;
		}

		if (!tsc.invariant) {
			printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "Warning: TSC is not invariant. Timing may drift.\n");
		}

		uint64_t freq = get_tsc_freq_cpuid();
		if (freq == 0) {
			freq = calibrate_tsc_with_pit();
		}

		return freq;
	}

#ifdef __cplusplus
}
#endif
#endif // WALLOS_TSC_H