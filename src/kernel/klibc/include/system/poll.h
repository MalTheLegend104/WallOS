#ifndef WALLOS_POLL_H
#define WALLOS_POLL_H
#ifdef __cplusplus
extern "C" {
#endif
	/* Never blocks, if another CPU is already polling it returns immediately.
	 * Safe with interrupts disabled */
	void system_poll_once(void);

	/* This was the old one, in theory shouldn't be used. */
	// void system_poll_loop(void);

	/* Create the poll task and its wake-up timer. Should only be called once. */
	void system_poll_start(void);

#ifdef __cplusplus
}
#endif
#endif // WALLOS_POLL_H