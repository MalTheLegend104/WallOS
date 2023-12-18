#include <timing.h>
#include <klibc/logger.h>
#include <terminal/terminal.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

enum date_format {
	DD_MM_YYYY,
	MM_DD_YYYY,
	YYYY_MM_DD
};

enum time_format {
	HOURS_12,
	HOURS_24
};

short current_date_format = DD_MM_YYYY;
short current_time_format = HOURS_24;

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
					printf("System Time: %dms\n", get_system_up_time());
					b++;
				}
				return 0;
			} else if (strcmp(argv[i], "-st") == 0 || strcmp(argv[i], "--system") == 0) {
				size_t time = get_system_up_time();
				size_t totalms = time;

				// Calculate years, months, days, hours, minutes, and seconds
				// Calculate years, months, days, hours, minutes, and seconds
				size_t years = time / (0x16BEE00); // 0x16BEE00 = 1000 * 60 * 60 * 24 * 365
				time %= (0x16BEE00);

				size_t months = time / (0x1C9C380); // 0x1C9C380 = 1000 * 60 * 60 * 24 * 30
				time %= (0x1C9C380);

				size_t days = time / (0x5265C00); // 0x5265C00 = 1000 * 60 * 60 * 24
				time %= (0x5265C00);

				size_t hours = time / (0x36EE80); // 0x36EE80 = 1000 * 60 * 60
				time %= (0x36EE80);

				size_t minutes = time / (0xEA60); // 0xEA60 = 1000 * 60
				time %= (0xEA60);

				size_t seconds = time / 0x3E8; // 0x3E8 = 1000
				time %= 0x3E8;

				set_colors(VGA_COLOR_CYAN, VGA_DEFAULT_BG);
				printf("Total Execution Time: %dms\n", totalms);
				printf("System has been up for:\n");
				if (years > 0) {
					printf("%lld Years.\n", years);
				}
				if (months > 0) {
					printf("%lld Months.\n", months);
				}
				if (days > 0) {
					printf("%lld Days.\n", days);
				}
				if (hours > 0) {
					printf("%lld Hours.\n", hours);
				}
				if (minutes > 0) {
					printf("%lld Minutes.\n", minutes);
				}
				if (seconds > 0) {
					printf("%lld Seconds.\n", seconds);
				}
				printf("%lld Milliseconds.\n", time);

				set_to_last();
				return 0;
			} else if (strcmp(argv[i], "-sdf") == 0 || strcmp(argv[i], "--set-date-format") == 0) {
				if (i == argc - 1) {
					logger(ERROR, "Additional argument is required. Run `help time -sdf` to see command usage.");
					return 0;
				}
				if (strcmp(argv[i + 1], "DMY") == 0 || strcmp(argv[i + 1], "dmy") == 0) {
					current_date_format = DD_MM_YYYY;
				} else if (strcmp(argv[i + 1], "MDY") == 0 || strcmp(argv[i + 1], "mdy") == 0) {
					current_date_format = MM_DD_YYYY;
				} else if (strcmp(argv[i + 1], "YMD") == 0 || strcmp(argv[i + 1], "ymd") == 0) {
					current_date_format = YYYY_MM_DD;
				} else {
					logger(ERROR, "Wrong argument provided. Run `help time -sdf` to see command usage.");
				}
				return 0;
			} else if (strcmp(argv[i], "-tf24") == 0 || strcmp(argv[i], "--time-format-24") == 0) {
				current_time_format = HOURS_24;
				return 0;
			} else if (strcmp(argv[i], "-tf12") == 0 || strcmp(argv[i], "--time-format-12") == 0) {
				current_time_format = HOURS_12;
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
	switch (current_time_format) {
		case HOURS_12:
			if (hours > 12) {
				hours -= 12;
				printf("%s:%s:%s PM ", format_int(h, 3, hours), format_int(m, 3, minutes), format_int(s, 3, seconds));
			} else {
				printf("%s:%s:%s AM ", format_int(h, 3, hours), format_int(m, 3, minutes), format_int(s, 3, seconds));
			}
			break;
		default: // 24 Hour Clock
			printf("%s:%s:%s ", format_int(h, 3, hours), format_int(m, 3, minutes), format_int(s, 3, seconds));
			break;
	}

	switch (current_date_format) {
		case DD_MM_YYYY:
			printf("%s/%s/%s\n",
				format_int(d, 3, day), format_int(mo, 3, month), format_int(y, 5, year)
			);
			break;
		case MM_DD_YYYY:
			printf("%s/%s/%s\n",
				format_int(mo, 3, month), format_int(d, 3, day), format_int(y, 5, year)
			);
			break;
		default: // YYYY-MM-DD
			printf("%s-%s-%s\n",
				format_int(y, 5, year), format_int(mo, 3, month), format_int(d, 3, day)
			);
			break;
	}

	set_to_last();

	return 0;
}

#pragma GCC diagnostic ignored "-Wunused-parameter" 
int time_help(int argc, char** argv) {
	// General help would be a little weird here since we deal with only flags and not subcommands
	if (argc > 1) {
		if (strcmp(argv[1], "--system") == 0 || strcmp(argv[1], "-st") == 0) {
			HelpEntry entry = {
				"Time (System Time)",
				"Displays system uptime.",
				NULL,
				0,
				NULL,
				0
			};
			printSpecificHelp(&entry);
			return 0;
		} else if (strcmp(argv[1], "--system-date-format") == 0 || strcmp(argv[1], "-sdf") == 0) {
			const char* required[] = {
				"<format> -> Date Time Format as specified in the optional section."
			};
			const char* optional[] = {
				"YMD      -> Sets the date format to YYYY-MM-DD",
				"MDY      -> Sets the date format to MM/DD/YYYY",
				"DMY      -> Sets the date format to DD/MM/YYYY"
			};
			HelpEntry entry = {
				"Time (Set Date Format)",
				"Changes the system Date Format.",
				required,
				1,
				optional,
				3
			};
			printSpecificHelp(&entry);
			return 0;
		} else if (strcmp(argv[1], "--test-accuracy") == 0 || strcmp(argv[1], "-ta") == 0) {
			const char* required[] = {
				"<time> -> Amount of seconds to test the accuracy."
			};
			HelpEntry entry = {
				"Time (Test Accuracy)",
				"Displays the accuracy of the internal timer, over <time> seconds.",
				required,
				1,
				NULL,
				0
			};
			printSpecificHelp(&entry);
			return 0;
		} else if (strcmp(argv[1], "-tf24") == 0 || strcmp(argv[1], "--time-format-24") == 0) {
			HelpEntry entry = {
				"Time (Time Format)",
				"Sets the displayed clock to 24h.",
				NULL,
				0,
				NULL,
				0
			};
			printSpecificHelp(&entry);
			return 0;
		} else if (strcmp(argv[1], "-tf12") == 0 || strcmp(argv[1], "--time-format-12") == 0) {
			HelpEntry entry = {
				"Time (Time Format)",
				"Sets the displayed clock to 12h.",
				NULL,
				0,
				NULL,
				0
			};
			printSpecificHelp(&entry);
			return 0;
		}
	}

	// Else is general help
	const char* optional[] = {
		"--system,",
		"-st           -> Prints the system uptime in milliseconds.\n",
		"--system-date-format",
		"-sdf <format> -> Changes the date format on the system.\n",
		"--time-format-24,",
		"-tf24         -> Changes the clock format to 24h.\n",
		"--time-format-12,",
		"-tf12         -> Changes the clock format to 12h.\n",
		"--test-accuracy <time>,",
		"-ta <time>    -> Test the accuracy of the system timer.\n",


		"If no flags are provided it will print the real world time (UTC-0).",

	};
	HelpEntry entry = {
		"Time",
		"Command to interface with the time subsystem.",
		NULL,
		0,
		optional,
		11
	};
	printSpecificHelp(&entry);

	return 0;
}