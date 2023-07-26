#include <timing.h>
#include <klibc/logger.h>
#include <terminal/terminal.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

bool is_int(const char* str) {
	if (str == NULL || *str == '\0') {
		// Null or empty string is not a base 10 integer.
		return false;
	}

	// Check each character of the string.
	while (*str != '\0') {
		if (*str < '0' || *str > '9') {
			// If the character is not a digit (0-9), the string is not a base 10 integer.
			return false;
		}
		str++; // Move to the next character in the string.
	}

	// If all characters are valid digits, the string is a base 10 integer.
	return true;
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
		outb(0x70, 0x0A);
		prev_status = inb(0x71);
	} while (prev_status & 0x80);

	// Read the time from CMOS registers
	outb(0x70, 0x04); // CMOS register index 0x04 stores the current hour
	*hours = inb(0x71);

	outb(0x70, 0x02); // CMOS register index 0x02 stores the current minutes
	*minutes = inb(0x71);

	outb(0x70, 0x00); // CMOS register index 0x00 stores the current seconds
	*seconds = inb(0x71);

	// Convert BCD to binary
	*hours = bcd_to_binary(*hours);
	*minutes = bcd_to_binary(*minutes);
	*seconds = bcd_to_binary(*seconds);

	// Enable interrupts again
	asm volatile("sti");
}

// Function to calculate the number of digits in an integer
int num_digits(int i) {
	int count = 0;
	if (i == 0)
		return 1;

	while (i != 0) {
		i /= 10;
		count++;
	}
	return count;
}

// Function to format the integer as a string with leading zeros
char* format_int(char* str, int size, int i) {
	// Calculate the number of digits in the integer
	int digits = num_digits(i);

	// Check if the buffer is large enough to hold the formatted string
	if (size - 1 < digits) {
		// Not enough space in the buffer, return NULL to indicate failure
		return NULL;
	}

	// Add leading zeros to the string
	for (int a = 0; a < size - 1 - digits; a++) {
		str[a] = '0';
	}

	// Convert the integer to a string representation
	for (int a = size - 2; a >= size - 1 - digits; a--) {
		str[a] = '0' + (i % 10);
		i /= 10;
	}

	str[size - 1] = '\0'; // Null-terminate the string

	// Return the pointer to the formatted string
	return str;
}

// Function to read the current date from CMOS
// Output: The current date in the format DD/MM/YYYY
void read_cmos_date(uint8_t* day, uint8_t* month, uint16_t* year) {
	// Disable interrupts to avoid any time discrepancies
	asm volatile("cli");

	// Wait for any previous update in progress to complete
	uint8_t prev_status;
	do {
		outb(0x70, 0x0A);
		prev_status = inb(0x71);
	} while (prev_status & 0x80);

	// Read the date from CMOS registers
	outb(0x70, 0x07); // CMOS register index 0x07 stores the current day of the month
	*day = inb(0x71);

	outb(0x70, 0x08); // CMOS register index 0x08 stores the current month
	*month = inb(0x71);

	outb(0x70, 0x09); // CMOS register index 0x09 stores the current year (last two digits)
	uint8_t low_year = inb(0x71);

	outb(0x70, 0x32); // CMOS register index 0x32 stores the current century
	uint8_t century = inb(0x71);

	// Convert BCD to binary
	*day = bcd_to_binary(*day);
	*month = bcd_to_binary(*month);
	low_year = bcd_to_binary(low_year);

	// Convert the century binary value to the decimal representation
	uint16_t current_century = (uint16_t) (100 * bcd_to_binary(century));

	// Combine the century and the year digits to get the full year
	*year = (uint16_t) (current_century + low_year);

	// Enable interrupts again
	asm volatile("sti");
}

int time_command(int argc, char** argv) {
	if (argc > 1) {
		for (int i = 1; i < argc; i++) {
			if (strcmp(argv[i], "--test-accuracy") == 0 || strcmp(argv[i], "-ta") == 0) {
				// No arg
				if (i + 1 >= argc) {
					logger(ERROR, "Expected argument after %s.\n", argv[i]);
					return 0;
				}
				// Next arg isn't an int.
				if (!is_int(argv[i + 1])) {
					logger(ERROR, "Unexpected argument after %s: %s\n", argv[i], argv[i + 1]);
					return 0;
				}
				int a = atoi(argv[i + 1]);
				int b = 0;
				while (b < a) {
					sleep(1000);
					printf("System Time: %d\n", get_system_up_time());
					b++;
				}
				return 0;
			} else if (strcmp(argv[i], "-st") == 0 || strcmp(argv[i], "--system") == 0) {
				printf("System Execution Time: %dms\n", get_system_up_time());
				return 0;
			}
		}
	}

	// No special flags
	uint8_t hours, minutes, seconds, day, month;
	uint16_t year;

	read_cmos_date(&month, &day, &year);
	read_cmos_time(&hours, &minutes, &seconds);
	char h[3];
	char m[3];
	char s[3];
	char d[3];
	char mo[3];
	char y[5];
	set_colors(VGA_COLOR_LIGHT_CYAN, VGA_DEFAULT_BG);
	printf("%s:%s:%s %s/%s/%s\n",
		format_int(h, 3, hours), format_int(m, 3, minutes), format_int(s, 3, seconds),
		format_int(d, 3, day), format_int(mo, 3, month), format_int(y, 5, year)
	);
	set_to_last();

	return 0;
}

int time_help(int argc, char** argv) {

	const char* optional[] = {
		"--system,",
		"-st         -> Prints the system uptime in milliseconds.\n",
		"--test-accuracy <time>,",
		"-ta <time>  -> Test the accuracy of the system timer.\n",
		"None -> Prints the real world time. (Does not take into account time zone).",

	};
	HelpEntry entry = {
		"Time",
		"Command to interface with the time subsystem.",
		NULL,
		0,
		optional,
		5
	};
	printSpecificHelp(&entry);

	return 0;
}