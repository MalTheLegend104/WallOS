#include <input/input_handler.h>
#include <system/timer.h>

#include <string.h>
#include <arch.h>

void input_wait_yield(void) {
	// Just halt and wait for an interrupt
	// This should probably just yield() whenever we get the scheduler
	cpu_hlt();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Queue Storage
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------


typedef struct {
	wallos_input_event_t events[WALLOS_INPUT_QUEUE_CAPACITY];
	volatile uint32_t head;
	volatile uint32_t tail;
	volatile uint32_t count;
	volatile uint32_t dropped; // events lost because the queue was full
} wallos_input_queue_t;

// One queue per wallos_input_device_type_t value
static wallos_input_queue_t input_queues[WALLOS_INPUT_DEVICE_MAX];

static inline bool queue_push(wallos_input_queue_t* q, const wallos_input_event_t* ev) {
	if (q->count == WALLOS_INPUT_QUEUE_CAPACITY) {
		q->dropped++;
		return false;
	}
	q->events[q->tail] = *ev;
	q->tail = (q->tail + 1) % WALLOS_INPUT_QUEUE_CAPACITY;
	q->count++;
	return true;
}

static inline bool queue_pop(wallos_input_queue_t* q, wallos_input_event_t* out) {
	if (q->count == 0) return false;
	*out = q->events[q->head];
	q->head = (q->head + 1) % WALLOS_INPUT_QUEUE_CAPACITY;
	q->count--;
	return true;
}

void input_push_event(const wallos_input_event_t* event) {
	if (!event) return;
	if (event->type > WALLOS_INPUT_DEVICE_MAX) return; // unknown device type, ignore

	wallos_input_event_t stamped = *event;
	if (stamped.timestamp_ms == 0) {
		stamped.timestamp_ms = timer_uptime_ms();
	}

	wallos_input_queue_t* q = &input_queues[stamped.type];

	cpu_disable_interrupts();
	queue_push(q, &stamped);
	cpu_enable_interrupts();
}

bool input_poll_event(wallos_input_device_type_t t, wallos_input_event_t* event) {
	if (!event) return false;
	if (t > WALLOS_INPUT_DEVICE_MAX) return false;

	wallos_input_queue_t* q = &input_queues[t];

	cpu_disable_interrupts();
	bool got = queue_pop(q, event);
	cpu_enable_interrupts();

	return got;
}

bool input_wait_event(wallos_input_device_type_t t, wallos_input_event_t* event) {
	if (!event) return false;
	if (t > WALLOS_INPUT_DEVICE_MAX) return false;

	while (!input_poll_event(t, event)) {
		input_wait_yield();
	}
	return true;
}

// I put this here for diagnostics, but don't really feel like wiring it up right now
uint32_t input_dropped_count(wallos_input_device_type_t t) {
	if (t > WALLOS_INPUT_DEVICE_MAX) return 0;
	return input_queues[t].dropped;
}