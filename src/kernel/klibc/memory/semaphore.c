#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include <memory/semaphore.h>
#include <memory/kernel_alloc.h>

#include <system/timing.h>

semaphore_t* semaphore_create(uint64_t max_count, uint64_t initial_count) {
	if (initial_count > max_count) {
		return NULL;  // Initial count cannot be greater than the max count
	}

	semaphore_t* sem = (semaphore_t*) kalloc(sizeof(semaphore_t));
	if (sem == NULL) {
		return NULL;  // Memory allocation failed
	}

	sem->max_count = max_count;
	sem->count = initial_count;

	return sem;
}

semaphore_status semaphore_wait(semaphore_t* sem, uint64_t units, uint64_t timeout) {
	if (sem == NULL) return SEMAPHORE_FAILURE;

	// In ACPICA: 
	// Timeout 0 means "return immediately if not available"
	// Timeout 0xFFFF means "wait forever"

	uint64_t time = 0;
	while (true) {
		uint64_t old_count = __atomic_load_n(&(sem->count), __ATOMIC_RELAXED);

		if (old_count >= units) {
			if (__atomic_compare_exchange_n(&(sem->count), &old_count, old_count - units,
				false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
				return SEMAPHORE_SUCCESS;
			}
			continue; // Retry immediately if CMPXCHG failed due to contention
		}

		// If we can't get it and timeout is 0, fail immediately
		if (timeout == 0) return SEMAPHORE_TIMEOUT;

		// If we have a timeout and reached it, fail
		if (timeout != 0xFFFF && time >= timeout) return SEMAPHORE_TIMEOUT;

		__builtin_ia32_pause();
		// Only sleep if you absolutely have to; for a spinlock-based sem, 
		// just pausing is better for short ACPI operations.
		time++;
	}
}

semaphore_status semaphore_signal(semaphore_t* sem, uint64_t units) {
	while (true) {
		uint64_t old_count = __atomic_load_n(&(sem->count), __ATOMIC_RELAXED);

		// Ensure we don't exceed the max count
		if (old_count < sem->max_count) {
			// Attempt to atomically increment the count
			if (__atomic_compare_exchange_n(&(sem->count), &old_count, old_count + units, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
				break;  // Successfully incremented
			}
		} else {
			return SEMAPHORE_OVER_MAX;
		}
	}
	return SEMAPHORE_SUCCESS;
}

void semaphore_destroy(semaphore_t* sem) {
	if (sem) {
		kfree(sem);
	}
}
