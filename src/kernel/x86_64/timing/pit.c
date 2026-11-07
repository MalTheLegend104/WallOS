#include <cpu_io.h>
#include <klibc/kprint.h>
#include <stdio.h>
#include <system/idt.h>
#include <system/timer.h>
#include <x86_64/timing.h>

#include <memory/kernel_alloc.h>

#define PIT_CHANNEL0_INPUT_HZ 1193182UL

extern bool pic_disabled;

static volatile uint64_t pit_ticks = 0;
static interval_clock_t pit_interval;
// static counter_clock_t   pit_counter;
static uint32_t pit_us_per_tick = 0;

static uint32_t pit_frequency_hz = 0; // last programmed rate, 0 = never initialised
static bool pit_registered = false;

void pit_handle_tick(void) {
	pit_ticks++;

	// On x86_64, we use the PIT as the dedicated "uptime" counter
	// It calls pit_handle_tick, we need to tell the timer subsystem we got a tick
	timer_tick_us(pit_us_per_tick);
}

// static uint64_t pit_counter_read(counter_clock_t* self) {
// 	(void) self;
// 	return pit_ticks;
// }

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

/* We need to be more specific unfortunately. Should probably make this what's actually used in arch.h. */

// Preserve the caller's IF state instead of forcing sti.
static inline uint64_t pit_irq_save(void) {
	uint64_t f;
	__asm__ volatile("pushfq; popq %0; cli" : "=r"(f)::"memory");
	return f;
}
static inline void pit_irq_restore(uint64_t f) {
	if (f & (1u << 9)) __asm__ volatile("sti" ::: "memory");
}

// Program channel 0 as a rate generator at `frequency_hz`.
// Touches hardware and pit_us_per_tick only
static void pit_program(uint32_t frequency_hz) {
	uint32_t divisor = PIT_CHANNEL0_INPUT_HZ / frequency_hz;
	if (divisor < 1) divisor = 1;
	if (divisor > 0xFFFF) divisor = 0xFFFF;

	pit_us_per_tick = 1000000UL / frequency_hz;
	pit_frequency_hz = frequency_hz;

	uint64_t flags = pit_irq_save();
	outb(0x43, 0x36); // ch0, lobyte/hibyte, mode 2, binary
	outb(0x40, (uint8_t) (divisor & 0xFF));
	outb(0x40, (uint8_t) ((divisor >> 8) & 0xFF));
	pit_irq_restore(flags);
}

/**
 * @brief Put the PIT back into periodic system-tick mode after something else borrowed it.
 *
 * Does NOT re-register the clock. Pass 0 to restore the previously programmed rate. Safe to call with interrupts on or off.
 */
void pit_reset(uint32_t frequency_hz) {
	if (frequency_hz == 0) frequency_hz = pit_frequency_hz;
	if (frequency_hz == 0) return; // pit_init() never ran, nothing to restore

	pit_program(frequency_hz);
	pit_interval.frequency_hz = frequency_hz;

	// With the PIC disabled the IOAPIC route (IRQ0 -> vector 32) already carries it.
	if (pic_disabled) return;

	uint64_t flags = pit_irq_save();
	irq_enable(0);
	pit_irq_restore(flags);
}

void pit_init(uint32_t frequency_hz) {
	if (pit_registered) { // already set up: just reprogram
		pit_reset(frequency_hz);
		return;
	}

	printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_COLOR_BLACK, "Install PIT at %uHz\n", frequency_hz);

	pit_program(frequency_hz);

	pit_interval = (interval_clock_t) {
		.name = "pit",
		.rating = 100,
		.frequency_hz = frequency_hz,
		.set_mode = pit_set_mode,
		.set_next_event = pit_set_next_event,
		.event_handler = NULL,
	};
	interval_clock_register(&pit_interval);
	pit_registered = true;

	if (pic_disabled) return;

	uint64_t flags = pit_irq_save();
	outb(0x21, 0xFD);
	irq_enable(0);
	pit_irq_restore(flags);
}
