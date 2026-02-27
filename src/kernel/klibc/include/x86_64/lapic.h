#ifndef WALLOS_LAPIC_H
#define WALLOS_LAPIC_H

#include <stdint.h>
#include <stdbool.h>
#include <cpu_io.h>

#include <x86_64/tsc.h>

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

uint32_t lapic_read(uint32_t offset);
void lapic_write(uint32_t offset, uint32_t value);

void lapic_send_ipi(uint8_t apic_id, uint32_t icr_low);

void set_lapic_base(uint64_t* base);

void bsp_init_lapic();
void ap_init_lapic();

uint64_t calibrate_lapic_timer_with_tsc(uint64_t tsc_freq);

void lapic_sleep_us(uint64_t lapic_freq, uint64_t us);

// extern void enable_lapic_msr(uint64_t lapic_phys);

static void enable_lapic_msr(uintptr_t phys_addr) {
	uint32_t low, high;

	// Read current MSR state
	// ecx = 0x1B
	__asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1B));

	// Preserve everything EXCEPT the address and the enable bit
	// Address must be 4KB aligned.
	// Low: Clear bits 12-31. High: Clear bits 0-19 (representing 32-51)
	low &= 0x00000FFF;
	high &= 0xFFF00000; // Adjust based on MAXPHYADDR, but this is usually safe

	// Set the Enable Bit (11)
	low |= (1 << 11);

	// Insert the new physical address
	// We assume phys_addr is 4KB aligned.
	low |= (uint32_t) (phys_addr & 0xFFFFF000);
	high |= (uint32_t) (phys_addr >> 32);

	// Write back
	__asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(0x1B));
}

#endif // WALLOS_LAPIC_H