#ifndef WALLOS_ARCH_X86_64_H
#define WALLOS_ARCH_X86_64_H

// ------------------------------------------------------------------------------------------------
// CPU execution
// ------------------------------------------------------------------------------------------------

/**
 * Pause/spin-loop hint.
 */
#define cpu_pause() __asm__ volatile("pause")

/**
 * Spin-loop relaxation with compiler memory ordering.
 * Provides the processor with a spin-loop hint and prevents the compiler from moving memory accesses across this operation.
 * This is NOT a hardware memory barrier.
 */
#define cpu_relax() __asm__ volatile("pause" ::: "memory")

// ------------------------------------------------------------------------------------------------
// CPU control
// ------------------------------------------------------------------------------------------------

/**
 * Halt the current CPU until an interrupt is received.
 */
#define cpu_hlt() __asm__ volatile("hlt")



#endif // WALLOS_ARCH_X86_64_H