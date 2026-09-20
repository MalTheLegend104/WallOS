#include <system/timer.h>
#include <x86_64/timing.h>
#include <cpu_io.h>

static counter_clock_t  io_delay_counter;
static volatile uint64_t io_delay_ticks = 0;

static uint64_t io_delay_read(counter_clock_t* self) {
	(void) self;
	outb(0x80, 0);
	return ++io_delay_ticks;
}

void io_delay_init(void) {
	io_delay_counter = (counter_clock_t){
		.name = "io_delay",
		.rating = 1, // lowest possible. everything should be better than this.
		.frequency_hz = 1000000, // ~1us/step assumed, probably not accurate
		.counter_bits = 64,
		.read = io_delay_read,
	};
	counter_clock_register(&io_delay_counter);
}