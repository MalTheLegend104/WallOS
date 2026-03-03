#ifndef WALLOS_LAPIC_H
#define WALLOS_LAPIC_H

#include <stdint.h>
#include <stdbool.h>
#include <cpu_io.h>

#include <x86_64/tsc.h>

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// LAPIC register definitions (and a couple of MISC defines)
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Local APIC ID Register.
// Bits 24-31 contain the physical APIC ID of this processor.
#define LAPIC_ID                0x020

// Local APIC Version Register.
// Contains LAPIC version and the number of supported LVT entries.
#define LAPIC_VERSION           0x030

// Task Priority Register (TPR).
// Controls interrupt acceptance threshold. Interrupts with priority <= TPR are blocked.
#define LAPIC_TPR               0x080

// Arbitration Priority Register (APR).
// Used internally for bus arbitration (we won't touch this).
#define LAPIC_APR               0x090

// Processor Priority Register (PPR).
// Reflects the current effective interrupt priority.
#define LAPIC_PPR               0x0A0

// End Of Interrupt (EOI).
// Write any value to signal completion of an interrupt.
#define LAPIC_EOI               0x0B0

// Remote Read Register (RRD).
// Used for APIC bus remote reads (we won't touch this).
#define LAPIC_RRD               0x0C0

// Logical Destination Register (LDR).
// Holds logical APIC ID when using logical destination mode.
#define LAPIC_LDR               0x0D0

// Destination Format Register (DFR).
// Defines logical APIC model (flat or clustered). Ignored in x2APIC mode.
#define LAPIC_DFR               0x0E0

// Spurious Interrupt Vector Register (SVR).
// Enables the LAPIC and sets the spurious interrupt vector. Bit 8 = APIC Software Enable.
#define LAPIC_SVR               0x0F0

// In-Service Registers (ISR)
// Indicates which interrupt vectors are currently being serviced.
// 256 bits total across ISR0-ISR7.
// A bit is set when the CPU has accepted the interrupt and not yet EOI'd it.
#define LAPIC_ISR0              0x100
#define LAPIC_ISR1              0x110
#define LAPIC_ISR2              0x120
#define LAPIC_ISR3              0x130
#define LAPIC_ISR4              0x140
#define LAPIC_ISR5              0x150
#define LAPIC_ISR6              0x160
#define LAPIC_ISR7              0x170

// Trigger Mode Registers (TMR)
// Indicates whether each interrupt vector is edge or level triggered.
// 256 bits total across TMR0-TMR7. Bit set = level triggered.
#define LAPIC_TMR0              0x180
#define LAPIC_TMR1              0x190
#define LAPIC_TMR2              0x1A0
#define LAPIC_TMR3              0x1B0
#define LAPIC_TMR4              0x1C0
#define LAPIC_TMR5              0x1D0
#define LAPIC_TMR6              0x1E0
#define LAPIC_TMR7              0x1F0

// Interrupt Request Registers (IRR)
// Indicates pending interrupts waiting to be serviced.
// 256 bits total across IRR0-IRR7.
// A bit is set when an interrupt has been accepted but not yet acknowledged.
#define LAPIC_IRR0              0x200
#define LAPIC_IRR1              0x210
#define LAPIC_IRR2              0x220
#define LAPIC_IRR3              0x230
#define LAPIC_IRR4              0x240
#define LAPIC_IRR5              0x250
#define LAPIC_IRR6              0x260
#define LAPIC_IRR7              0x270

// Error Status Register (ESR).
// Must be written twice to clear (per Intel spec).
#define LAPIC_ESR               0x280 // Records APIC transmission and reception errors.

// Interrupt Command Register (ICR)
#define LAPIC_ICR_LOW           0x300 // ICR Low (control bits: vector, delivery mode, level, trigger, shorthand).
#define LAPIC_ICR_HIGH          0x310 // ICR High (destination field: physical or logical APIC ID).

// Local Vector Table (LVT)
#define LAPIC_LVT_TIMER         0x320 // LAPIC Timer LVT entry. Controls timer interrupt vector, mask, and mode.
#define LAPIC_LVT_THERMAL       0x330 // Thermal Sensor LVT entry. Interrupt generated on thermal events.
#define LAPIC_LVT_PERF          0x340 // Performance Monitoring Counter LVT entry. Interrupt for performance counter overflow.
#define LAPIC_LVT_LINT0         0x350 // LINT0 LVT entry. Typically connected to external interrupt (ExtINT) or masked.
#define LAPIC_LVT_LINT1         0x360 // LINT1 LVT entry. Typically used for NMI delivery.
#define LAPIC_LVT_ERROR         0x370 // Error LVT entry. Vector used when LAPIC detects internal errors.

// Timer Registers
#define LAPIC_INITIAL_COUNT     0x380 // Initial Count Register. Value loaded into the LAPIC timer counter.
#define LAPIC_CURRENT_COUNT     0x390 // Current Count Register. Decrements from initial value at LAPIC timer frequency.
#define LAPIC_DIVIDE_CONFIG     0x3E0 // Divide Configuration Register. Controls the timer clock divisor (1,2,4,8,16,32,64,128).

// MISC
#define SPURIOUS_VECTOR         0xFF // Spurious Int Vector

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// ICR Register Defines
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Bits 0-7: Interrupt vector (0-255).
//
// Meaning depends on Delivery Mode:
// 	FIXED / LOWEST PRIORITY:
//		- Vector number delivered to the target CPU.
//		- CPU invokes IDT[vector] just like a normal interrupt.
//
//	NMI:
//		- Vector field is ignored.
//		- CPU executes the NMI handler (vector 2 internally).
//
//	SMI:
//		- Vector field is ignored.
//		- CPU enters System Management Mode (SMM).
//		- We will never use this.
//
//	INIT:
//		- Vector field is ignored.
//		- Causes target processor to reset (used in SMP bring-up).
//
//	STARTUP (SIPI):
//		- Vector specifies the 4KB page number of the startup code.
//		- Execution begins at physical address (vector << 12).
//
//	Notes:
//		- Interrupts should be above 32, the lower 32 are reserved for regular IDT.
#define LAPIC_ICR_VECTOR(x)      ((x) & 0xFF)

// Delivery Mode (bits 8-10)
#define LAPIC_ICR_DM_FIXED			(0x0 << 8) // Normal interrupt delivery to the specified vector.
#define LAPIC_ICR_DM_LOWEST			(0x1 << 8) // Deliver interrupt to the lowest-priority CPU among the destination set.
#define LAPIC_ICR_DM_SMI			(0x2 << 8) // Enters SMM (we will never touch this, I just want it defined)
#define LAPIC_ICR_DM_NMI			(0x4 << 8) // Non-Maskable Interrupt
#define LAPIC_ICR_DM_INIT			(0x5 << 8) // Init IPI. Used during SMP bring-up to reset an AP.
#define LAPIC_ICR_DM_STARTUP		(0x6 << 8) // Startup IPI. Causes an AP to begin execution at vector << 12.

// Destination Mode (bit 11)
#define LAPIC_ICR_DEST_PHYSICAL		(0x0 << 11) // Destination field refers to physical APIC ID.
#define LAPIC_ICR_DEST_LOGICAL		(0x1 << 11) // Destination field refers to logical APIC ID (cluster/flat mode).

// Level (bit 14)
#define LAPIC_ICR_LEVEL_DEASSERT	(0x0 << 14) // Deassert signal (used mainly for INIT deassert in level-triggered mode).
#define LAPIC_ICR_LEVEL_ASSERT		(0x1 << 14) // Assert signal (normal case when sending INIT/SIPI).

// Trigger Mode (bit 15)
#define LAPIC_ICR_TRIGGER_EDGE		(0x0 << 15) // Edge-triggered interrupt (most IPIs use this).
#define LAPIC_ICR_TRIGGER_LEVEL		(0x1 << 15) // Level-triggered interrupt (required for INIT IPIs).

// Destination Shorthand (bits 18-19)
#define LAPIC_ICR_SH_NONE			(0x0 << 18) // No shorthand, use ICR high register destination field.
#define LAPIC_ICR_SH_SELF			(0x1 << 18) // Send to self.
#define LAPIC_ICR_SH_ALL_INC		(0x2 << 18) // Send to all processors including self.
#define LAPIC_ICR_SH_ALL_EXC		(0x3 << 18) // Send to all processors except self.

/**
 * @brief Read from the LAPIC.
 *
 * @param offset Register in the LAPIC to read.
 * @return uint32_t Value in the register.
 */
uint32_t lapic_read(uint32_t offset);

/**
 * @brief Write to the LAPIC
 *
 * @param offset Register in the LAPIC to write to
 * @param value Value to write to the register
 */
void lapic_write(uint32_t offset, uint32_t value);

/**
 * @brief Send an Inter-Processor Interrupt (IPI).
 *
 * Writes the destination APIC ID to ICR_HIGH and the provided
 * control bits to ICR_LOW. This function internally waits
 * for the Delivery Status bit to clear before returning.
 *
 * @param apic_id Physical APIC ID of the target processor.
 * @param icr_low Pre-constructed ICR low value (vector + flags).
 */
void lapic_send_ipi(uint8_t apic_id, uint32_t icr_low);

/**
 * @brief Set the virtual base address of the mapped LAPIC.
 *
 * The base should be mapped into the kernel's virtual address space.
 * Should only be called once by the BSP during init, unless the base gets remapped.
 *
 * @param base Virtual address of the mapped LAPIC registers.
 */
void set_lapic_base(uint64_t* base);

/**
 * @brief Initialize the LAPIC on the Bootstrap Processor (BSP).
 *
 * This function does the following:
 *  - Setting the Spurious Interrupt Vector
 *  - Configuring LVT entries
 *  - Clearing pending errors
 *
 * It is expected that the caller has already enabled enabled the LAPIC using enable_lapic_msr().
 * This should only be called once on the BSP during early SMP setup.
 */
void bsp_init_lapic();

/**
 * @brief Initialize the LAPIC on an Application Processor (AP).
 *
 * Performs per-core LAPIC setup after the AP has started via SIPI.
 * Typically enables the LAPIC, sets the spurious vector, and
 * configures required LVT entries.
 *
 * Should be called during AP startup after switching to long mode.
 */
void ap_init_lapic();

/**
 * @brief Calibrate the LAPIC timer using the TSC.
 *
 * Measures LAPIC timer decrement rate against the Time Stamp Counter to determine the LAPIC timer frequency.
 *
 * @param tsc_freq Known TSC frequency in Hz.
 * @return uint64_t Calculated LAPIC timer frequency in Hz.
 */
uint64_t calibrate_lapic_timer_with_tsc(uint64_t tsc_freq);

/**
 * @brief Sleep using the LAPIC timer.
 *
 * This should only be used for very short delay, while the normal system timer isn't setup.
 *
 * @param lapic_freq LAPIC timer frequency in Hz.
 * @param us Duration to sleep in microseconds.
 */
void lapic_sleep_us(uint64_t lapic_freq, uint64_t us);

/**
 * @brief Enable the LAPIC via the IA32_APIC_BASE MSR.
 *
 * Sets the APIC Global Enable bit in the APIC base MSR and
 * configures the physical base address if required.
 *
 * Must be called before accessing LAPIC registers.
 *
 * @param phys_addr Physical address of the LAPIC register page.
 */
void enable_lapic_msr(uintptr_t phys_addr);

#endif // WALLOS_LAPIC_H