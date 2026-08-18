#include <system/timer.h>
#include <x86_64/timing.h>
#include <klibc/kprint.h>
#include <cpu_io.h>
#include <system/idt.h>
#include <stdio.h>

#include <memory/kernel_alloc.h>

#define PIT_CHANNEL0_INPUT_HZ 1193182UL

extern bool pic_disabled;

static volatile uint64_t pit_ticks = 0;
static interval_clock_t  pit_interval;
static counter_clock_t   pit_counter;
static uint32_t          pit_us_per_tick = 0;

void pit_handle_tick(void) {
	pit_ticks++;

	// On x86_64, we use the PIT as the dedicated "uptime" counter
	// It calls pit_handle_tick, we need to tell the 
	timer_tick_us(pit_us_per_tick);
}

static uint64_t pit_counter_read(counter_clock_t* self) {
	(void) self;
	return pit_ticks;
}

static void pit_set_mode(interval_clock_t* self, interval_clock_mode_t mode) {
	(void) self;
	if (pic_disabled) return;

	switch (mode) {
		case INTERVAL_CLOCK_SHUTDOWN:
			__asm__ volatile("cli");
			outb(0x21, inb(0x21) | 0x01); // mask IRQ0
			__asm__ volatile("sti");
			break;
		case INTERVAL_CLOCK_PERIODIC:
			__asm__ volatile("cli");
			outb(0x21, inb(0x21) & ~0x01); // unmask IRQ0 
			__asm__ volatile("sti");
			break;
		case INTERVAL_CLOCK_ONESHOT:
			 // HPET and APIC are way better for oneshot, not to mention we'd need to take it out of periodic.
			break;
	}
}

static void pit_set_next_event(interval_clock_t* self, uint64_t ticks) {
	(void) self;
	(void) ticks;
	// Rate is fixed at 1ms
}

void pit_init_dev() {
	wallos_device_t* dev = (wallos_device_t*) kcalloc(1, sizeof(wallos_device_t));
	dev->interfaces = DEV_INT_TIMER | DEV_INT_ALREADY_BOUND;
	dev->parent = get_root_timer();
	dev->name = "PIT";
	register_device(dev);
	// this timer should live as long as the system does, we don't worry about cleanup
}

void pit_init(uint32_t frequency_hz) {
	printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_COLOR_BLACK, "Install PIT at %uHz\n", frequency_hz);

	uint32_t divisor = PIT_CHANNEL0_INPUT_HZ / frequency_hz;
	uint8_t  low = (uint8_t) (divisor & 0xFF);
	uint8_t  high = (uint8_t) ((divisor >> 8) & 0xFF);

	// Microseconds per tick
	// Exact when the frequency divides 1 MHz, otherwise truncates slightly
	pit_us_per_tick = (frequency_hz > 0) ? (1000000UL / frequency_hz) : 0;

	// Channel 0, mode 2 (rate generator), lobyte/hibyte access
	outb(0x43, 0x36);
	outb(0x40, low);
	outb(0x40, high);

	pit_interval = (interval_clock_t){
		.name = "pit",
		.rating = 100,
		.frequency_hz = frequency_hz,
		.set_mode = pit_set_mode,
		.set_next_event = pit_set_next_event,
		.event_handler = NULL,
	};
	interval_clock_register(&pit_interval);

	// Since we set this up to a 1ms interval (roughly), I don't really want to register this as a counter clock
	// Even just stalling with io_delay is better for VERY short waits (microsecond) than waiting a whole 1ms when something needs a short wait.
	// pit_counter = (counter_clock_t){
	// 	.name = "pit",
	// 	.rating = 50,
	// 	.frequency_hz = frequency_hz,
	// 	.counter_bits = 64,
	// 	.read = pit_counter_read,
	// };
	// counter_clock_register(&pit_counter);

	if (pic_disabled) return;

	__asm__ volatile("cli");
	outb(0x21, 0xFD);
	irq_enable(0);
	__asm__ volatile("sti");
}