#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct {
		uint64_t count;
		uint64_t max_count;
	} semaphore_t;

	typedef enum {
		SEMAPHORE_SUCCESS = 0,
		SEMAPHORE_TIMEOUT,
		SEMAPHORE_OVER_MAX,
		SEMAPHORE_FAILURE,
	} semaphore_status;

	semaphore_t* semaphore_create(uint64_t max_count, uint64_t initial_count);
	void semaphore_destroy(semaphore_t* sem);

	semaphore_status semaphore_wait(semaphore_t* sem, uint64_t units, uint64_t timeout);
	semaphore_status semaphore_signal(semaphore_t* sem, uint64_t units);

#ifdef __cplusplus
}
#endif

#endif // SEMAPHORE_H