#include <system/timer.h>
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

// Picks a "human sized" unit for Hz, and rounds the value to a whole number for that
// This avoids us having hard to parse values in timer info
static const char* format_freq(uint64_t hz, uint64_t* out_val) {
	if (hz >= 1000000000ULL) { *out_val = (hz + 500000000ULL) / 1000000000ULL; return "GHz"; }
	if (hz >= 1000000ULL) { *out_val = (hz + 500000ULL) / 1000000ULL; return "MHz"; }
	if (hz >= 1000ULL) { *out_val = (hz + 500ULL) / 1000ULL; return "kHz"; }
	*out_val = hz;
	return "Hz";
}

// Same idea as format_freq but for time in ns.
static const char* format_duration_ns(uint64_t ns, uint64_t* out_val) {
	if (ns >= 1000000ULL) { *out_val = (ns + 500000ULL) / 1000000ULL; return "ms"; }
	if (ns >= 1000ULL) { *out_val = (ns + 500ULL) / 1000ULL; return "us"; }
	*out_val = ns;
	return "ns";
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
					busy_wait_ms(1000);
					printf("System Time: %lldms\n", (long long) timer_uptime_ms());
					b++;
				}
				return 0;
			} else if (strcmp(argv[i], "-st") == 0 || strcmp(argv[i], "--system") == 0) {
				uint64_t time = timer_uptime_ms();
				uint64_t totalms = time;

				// Calculate years, months, days, hours, minutes, and seconds
				uint64_t years = time / (0x16BEE00); // 0x16BEE00 = 1000 * 60 * 60 * 24 * 365
				time %= (0x16BEE00);

				uint64_t months = time / (0x1C9C380); // 0x1C9C380 = 1000 * 60 * 60 * 24 * 30
				time %= (0x1C9C380);

				uint64_t days = time / (0x5265C00); // 0x5265C00 = 1000 * 60 * 60 * 24
				time %= (0x5265C00);

				uint64_t hours = time / (0x36EE80); // 0x36EE80 = 1000 * 60 * 60
				time %= (0x36EE80);

				uint64_t minutes = time / (0xEA60); // 0xEA60 = 1000 * 60
				time %= (0xEA60);

				uint64_t seconds = time / 0x3E8; // 0x3E8 = 1000
				time %= 0x3E8;

				display_set_colors(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG);
				printf("Total Execution Time: %lldms\n", (long long) totalms);
				printf("System has been up for:\n");
				if (years > 0) {
					printf("%lld Years.\n", (long long) years);
				}
				if (months > 0) {
					printf("%lld Months.\n", (long long) months);
				}
				if (days > 0) {
					printf("%lld Days.\n", (long long) days);
				}
				if (hours > 0) {
					printf("%lld Hours.\n", (long long) hours);
				}
				if (minutes > 0) {
					printf("%lld Minutes.\n", (long long) minutes);
				}
				if (seconds > 0) {
					printf("%lld Seconds.\n", (long long) seconds);
				}
				printf("%lld Milliseconds.\n", (long long) time);

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
			} else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--info") == 0) {
				printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "Timers  (* = active)\n");

				counter_clock_t* best_counter = counter_clock_get_best();
				printf("Counter:\n");

				// all of the prints in this section are awful, just ignore them...
				if (!counter_clock_get_first()) printf("  none\n");
				for (counter_clock_t* c = counter_clock_get_first(); c; c = c->next) {
					uint64_t freq_val, res_val;
					const char* freq_unit = format_freq(c->frequency_hz, &freq_val);
					// Rounded rather than truncated so a short tick period doesn't misleadingly print as "0"
					uint64_t resolution_ns = (1000000000ULL + c->frequency_hz / 2) / c->frequency_hz;
					const char* res_unit = format_duration_ns(resolution_ns, &res_val);
					printf(" %s%-8s %4lld%-3s r%-3d %2dbit ~%lld%s\n", (c == best_counter) ? "*" : " ", c->name, (long long) freq_val, freq_unit, c->rating, c->counter_bits, (long long) res_val, res_unit);
				}

				interval_clock_t* best_interval = interval_clock_get_best();
				printf("Interval:\n");
				if (!interval_clock_get_first()) printf("  none\n");
				for (interval_clock_t* c = interval_clock_get_first(); c; c = c->next) {
					uint64_t freq_val;
					const char* freq_unit = format_freq(c->frequency_hz, &freq_val);
					printf(" %s%-8s %4lld%-3s r%-3d\n", (c == best_interval) ? "*" : " ", c->name, (long long) freq_val, freq_unit, c->rating);
				}

				wallclock_t* best_wallclock = wallclock_get_best();
				printf("Wallclock:\n");
				if (!wallclock_get_first()) printf("  none\n");
				for (wallclock_t* c = wallclock_get_first(); c; c = c->next) {
					printf(" %s%-8s r%-3d %s\n", (c == best_wallclock) ? "*" : " ", c->name, c->rating, c->write ? "rw" : "ro");
				}

				printf("Uptime: %lldms\n", (long long) timer_uptime_ms());
				return 0;
			}
		}
	}

	// No special flags
	wall_time_t now;
	if (!wallclock_read(&now)) {
		logger(ERROR, "No wallclock is registered - can't display real-world time.\n");
		return 0;
	}
	uint8_t hours = now.hour, minutes = now.minute, seconds = now.second;
	uint8_t day = now.day, month = now.month;
	uint16_t year = now.year;

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
		} else if (strcmp(argv[1], "-i") == 0 || strcmp(argv[1], "--info") == 0) {
			HelpEntry entry = {
				"Time (Info)",
				"Lists all registered counter_clock/interval_clock/wallclock devices, which one is active for each role, and their rating/frequency/resolution.",
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
		"--info,",
		"-i            -> Shows registered timer devices and which is active.\n",


		"If no flags are provided it will print the real world time (UTC-0).",

	};
	HelpEntry entry = {
		"Time",
		"Command to interface with the time subsystem.",
		NULL,
		0,
		optional,
		13
	};
	printSpecificHelp(&entry);

	return 0;
}