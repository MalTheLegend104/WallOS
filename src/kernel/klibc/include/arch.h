#ifndef WALLOS_ARCH_H
#define WALLOS_ARCH_H

#ifdef __cplusplus
extern "C" {
#endif
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Architecture independent
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	// ------------------------------------------------------------------------------------------------
	// Memory Barriers
	// ------------------------------------------------------------------------------------------------

	/**
	 * @brief Full memory barrier.
	 * Prevents memory operations before the barrier from being reordered with memory operations after the barrier.
	 */
	static inline void cpu_memory_barrier(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
	/**
	 * @brief Read/acquire memory barrier.
	 * Prevents subsequent memory operations from being reordered before the barrier.
	 */
	static inline void cpu_read_barrier(void) { __atomic_thread_fence(__ATOMIC_ACQUIRE); }
	/**
	 * @brief Write/release memory barrier.
	 * Prevents preceding memory operations from being reordered after the barrier.
	 */
	static inline void cpu_write_barrier(void) { __atomic_thread_fence(__ATOMIC_RELEASE); }
	/**
	 * @brief Compiler-only memory barrier.
	 * Prevents the compiler from reordering memory accesses across this point.
	 */
	static inline void cpu_compiler_barrier(void) { __asm__ volatile("" ::: "memory"); }

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Architecture Dependents
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
#if defined(WALLOS_ARCH_X86_64)
#include <x86_64/arch.h>
#else
#error "Unsupported architecture"
#endif

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
}
#endif
#endif // WALLOS_ARCH_H