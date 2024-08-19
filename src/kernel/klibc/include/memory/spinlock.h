#ifndef SPINLOCK_H
#define SPINLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <memory/semaphore.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h> // For malloc and free

	typedef struct {
		semaphore_t* sem;
	} spinlock_t;

	spinlock_t* spinlock_create(void);
	void spinlock_lock(spinlock_t* lock);
	void spinlock_unlock(spinlock_t* lock);
	void spinlock_destroy(spinlock_t* lock);
#ifdef __cplusplus
}
#endif
#endif // SPINLOCK_H