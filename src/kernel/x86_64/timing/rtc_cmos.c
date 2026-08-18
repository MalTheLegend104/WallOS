#include <system/timer.h>
#include <x86_64/timing.h>
#include <cpu_io.h>
#include <endian_bits.h>

#define CMOS_OUT_PORT 0x70
#define CMOS_IN_PORT  0x71
#define CMOS_SECONDS  0x00
#define CMOS_MINUTES  0x02
#define CMOS_HOURS    0x04
#define CMOS_DAY      0x07
#define CMOS_MONTH    0x08
#define CMOS_YEAR     0x09
#define CMOS_CENTURY  0x32
#define CMOS_STATUS_A 0x0A

static wallclock_t cmos_wallclock;

static uint8_t cmos_read_reg(uint8_t reg) {
	outb(CMOS_OUT_PORT, reg);
	return inb(CMOS_IN_PORT);
}

static void cmos_wait_for_update(void) {
	uint8_t status;
	do {
		status = cmos_read_reg(CMOS_STATUS_A);
	} while (status & 0x80);
}

// TODO: I copied this from ktime but updated it to use the endian_bits.h helpers instead.
// This technically should check for Status Register B bit 2 to see if we are in binary mode
// The default *should* be BCD, and we never change it, but still wanted to note it in case I notice time being weird
void cmos_read(wallclock_t* self, wall_time_t* out) {
	(void) self;

	__asm__ volatile("cli");
	cmos_wait_for_update();

	uint8_t hours = cmos_read_reg(CMOS_HOURS);
	uint8_t minutes = cmos_read_reg(CMOS_MINUTES);
	uint8_t seconds = cmos_read_reg(CMOS_SECONDS);
	uint8_t day = cmos_read_reg(CMOS_DAY);
	uint8_t month = cmos_read_reg(CMOS_MONTH);
	uint8_t year_lo = cmos_read_reg(CMOS_YEAR);
	uint8_t century = cmos_read_reg(CMOS_CENTURY);

	__asm__ volatile("sti");

	out->hour = bcd_to_bin8(hours);
	out->minute = bcd_to_bin8(minutes);
	out->second = bcd_to_bin8(seconds);
	out->day = bcd_to_bin8(day);
	out->month = bcd_to_bin8(month);
	out->year = (uint16_t) (bcd_to_bin8(century) * 100 + bcd_to_bin8(year_lo));
}

void rtc_cmos_init(void) {
	cmos_wallclock = (wallclock_t){
		.name = "cmos_rtc",
		.rating = 100,
		.read = cmos_read,
		.write = NULL, // read-only for now, could technically write back to it but I don't really feel like dealing with that.
	};
	wallclock_register(&cmos_wallclock);
}