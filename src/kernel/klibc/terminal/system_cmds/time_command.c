#include <system/timing.h>
#include <system/ktime.h>
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

	// Check each character of the string to make sure it's base 10.
	while (*str != '\0') {
		if (*str < '0' || *str > '9') {
			return false;
		}
		str++;
	}

	// If all characters are valid digits, the string is a base 10 integer.
	return true;
}

int time_command(int argc, char** argv) {
	if (argc > 1) {
		for (int i = 1; i < argc; i++) {
			if (strcmp(argv[i], "--test-accuracy") == 0 || strcmp(argv[i], "-ta") == 0) {
				// No arg
				if (i + 1 >= argc) {
					logger(ERROR, "Expected argument after %02d.\n", argv[i]);
					return 0;
				}
				// Next arg isn't an int.
				if (!is_int(argv[i + 1])) {
					logger(ERROR, "Unexpected argument after %02d: %02d\n", argv[i], argv[i + 1]);
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

				display_set_colors(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG);
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

				display_set_colors_default();
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

	read_cmos_date(&day, &month, &year);
	read_cmos_time(&hours, &minutes, &seconds);
	display_set_colors(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG);

	if (current_time_format == HOURS_12) {
		if (hours > 12) {
			hours -= 12;
			printf("%02d:%02d:%02d PM ", hours, minutes, seconds);
		} else {
			printf("%02d:%02d:%02d AM ", hours, minutes, seconds);
		}
	} else {
		printf("%02d:%02d:%02d ", hours, minutes, seconds);
	}

	switch (current_date_format) {
		case DD_MM_YYYY:
			printf("%02d/%02d/%02d\n", day, month, year);
			break;
		case MM_DD_YYYY:
			printf("%02d/%02d/%02d\n", month, day, year);
			break;
		default: // YYYY-MM-DD
			printf("%02d-%02d-%02d\n", year, month, day);
			break;
	}

	display_set_colors_default();

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