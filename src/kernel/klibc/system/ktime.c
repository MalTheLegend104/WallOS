#include <system/ktime.h>
#include <stdio.h>
#include <cpu_io.h>

void print_fattime(uint32_t fdate, uint32_t ftime) {
	// fdate: bits 9-15 (Year-1980), 5-8 (Month), 0-4 (Day)
	// ftime: bits 11-15 (Hour), 5-10 (Minute), 0-4 (Second/2)

	// Extract fields
	uint16_t year = (uint16_t) ((fdate >> 9) + 1980);
	uint8_t month = (uint8_t) ((fdate >> 5) & 0x0F);
	uint8_t day = (uint8_t) (fdate & 0x1F);
	uint8_t hour = (uint8_t) ((ftime >> 11) & 0x1F);
	uint8_t minute = (uint8_t) ((ftime >> 5) & 0x3F);
	uint8_t second = (uint8_t) ((ftime & 0x1F) * 2);

	// Use padding with '0' if your printf supports it (e.g., %02u)
	// If not, use conditional printing. Assuming standard printf for now.
	printf("%04u-%02u-%02u %02u:%02u:%02u", year, month, day, hour, minute, second);
}

static uint8_t bcd_to_binary(uint8_t bcd_value) {
	return ((bcd_value / 16) * 10) + (bcd_value % 16);
}

void read_cmos_time(uint8_t* hours, uint8_t* minutes, uint8_t* seconds) {
	// Disable interrupts to avoid any time discrepancies
	asm volatile("cli");

	// Wait for any previous update in progress to complete
	uint8_t prev_status;
	do {
		outb(CMOS_OUT_PORT, CMOS_STATUS_A);
		prev_status = inb(CMOS_IN_PORT);
	} while (prev_status & 0x80);

	// Read the time from CMOS registers
	outb(CMOS_OUT_PORT, CMOS_HOURS);
	*hours = inb(CMOS_IN_PORT);

	outb(CMOS_OUT_PORT, CMOS_MINUTES);
	*minutes = inb(CMOS_IN_PORT);

	outb(CMOS_OUT_PORT, CMOS_SECONDS);
	*seconds = inb(CMOS_IN_PORT);

	// Enable interrupts again
	// This is here to minimize time spent without interrupts.
	asm volatile("sti");

	// Convert BCD to binary
	*hours = bcd_to_binary(*hours);
	*minutes = bcd_to_binary(*minutes);
	*seconds = bcd_to_binary(*seconds);
}

// Function to read the current date from CMOS
// Output: The current date in the format DD/MM/YYYY
void read_cmos_date(uint8_t* day, uint8_t* month, uint16_t* year) {
	// Disable interrupts to avoid any time discrepancies
	asm volatile("cli");

	// Wait for any previous update in progress to complete
	uint8_t prev_status;
	do {
		outb(CMOS_OUT_PORT, CMOS_STATUS_A);
		prev_status = inb(CMOS_IN_PORT);
	} while (prev_status & 0x80);

	// Read the date from CMOS registers
	outb(CMOS_OUT_PORT, CMOS_DAY);
	*day = inb(CMOS_IN_PORT);

	outb(CMOS_OUT_PORT, CMOS_MONTH);
	*month = inb(CMOS_IN_PORT);

	outb(CMOS_OUT_PORT, CMOS_YEAR); // CMOS register index 0x09 stores the current year (last two digits)
	uint8_t low_year = inb(CMOS_IN_PORT);

	outb(CMOS_OUT_PORT, CMOS_CENTURY);
	uint8_t century = inb(CMOS_IN_PORT);

	// Enable interrupts again
	asm volatile("sti");

	// Convert BCD to binary
	*day = bcd_to_binary(*day);
	*month = bcd_to_binary(*month);
	low_year = bcd_to_binary(low_year);

	// Convert the century binary value to the decimal representation
	uint16_t current_century = (uint16_t) (100 * bcd_to_binary(century));

	// Combine the century and the year digits to get the full year
	*year = (uint16_t) (current_century + low_year);
}

uint32_t get_system_msdos_time() {
	uint8_t hours, minutes, seconds;
	uint8_t day, month;
	uint16_t year;

	read_cmos_time(&hours, &minutes, &seconds);
	read_cmos_date(&day, &month, &year);

	// MS-DOS time format:
	// Bits 0-4: seconds divided by 2
	// Bits 5-10: minutes
	// Bits 11-15: hours
	uint16_t msdos_time = (uint16_t) (((hours & 0x1F) << 11) | ((minutes & 0x3F) << 5) | ((seconds / 2) & 0x1F));

	// MS-DOS date format:
	// Bits 0-4: day
	// Bits 5-8: month
	// Bits 9-15: years since 1980
	uint16_t msdos_date = (uint16_t) ((((year - 1980) & 0x7F) << 9) | ((month & 0x0F) << 5) | (day & 0x1F));

	return ((uint32_t) msdos_date << 16) | msdos_time;
}