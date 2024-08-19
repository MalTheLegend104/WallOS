#include <memory/spinlock.h>
#include <memory/kernel_alloc.h>

/* This spinlock is mostly just a wrapper around semaphores. */

spinlock_t* spinlock_create(void) {
	spinlock_t* lock = (spinlock_t*) kalloc(sizeof(spinlock_t));
	if (lock) {
		lock->sem = semaphore_create(1, 1);
		if (!lock->sem) {
			kfree(lock);
			return NULL;
		}
	}
	return lock;
}

void spinlock_lock(spinlock_t* lock) {
	while (semaphore_wait(lock->sem, 1, 0) != SEMAPHORE_SUCCESS) {
		// Hopefully stops the cpu usage of regular busy-waiting.
		__builtin_ia32_pause();
	}
}

void spinlock_unlock(spinlock_t* lock) {
	semaphore_signal(lock->sem, 1);
}

// Destroy the spinlock
void spinlock_destroy(spinlock_t* lock) {
	if (lock) {
		semaphore_destroy(lock->sem);
		kfree(lock);
	}
}