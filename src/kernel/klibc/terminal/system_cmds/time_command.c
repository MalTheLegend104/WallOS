#include <system/timer.h>
#include <klibc/logger.h>
#include <terminal/terminal.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <terminal/wall_shell.h>

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

// Referenced from system_commands.c's registerSystemCommands().
const ws_command_argument_t time_args[] = {
	{ WS_ARG_TYPE_UINT32, false, "--test-accuracy",   "-ta",   "Tests the accuracy of the internal timer over <seconds>." },
	{ WS_ARG_TYPE_FLAG,   false, "--system",          "-st",   "Prints the system uptime." },
	{ WS_ARG_TYPE_STRING, false, "--set-date-format", "-sdf",  "Sets the date format. One of: DMY, MDY, YMD." },
	{ WS_ARG_TYPE_FLAG,   false, "--time-format-24",  "-tf24", "Sets the displayed clock to 24h." },
	{ WS_ARG_TYPE_FLAG,   false, "--time-format-12",  "-tf12", "Sets the displayed clock to 12h." },
	{ WS_ARG_TYPE_FLAG,   false, "--info",            "-i",    "Lists registered timer devices and which is active." },
};
const size_t time_args_count = sizeof(time_args) / sizeof(time_args[0]);

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
		ws_context_t* ctx = ws_getCurrentContext();

		if (ws_parse_args(ctx, argc, argv)) {
			if (ws_has_arg(ctx, "--test-accuracy")) {
				uint32_t seconds = (uint32_t) ws_get_uint64(ctx, "--test-accuracy");
				uint32_t b = 0;
				while (b < seconds) {
					busy_wait_ms(1000);
					printf("System Time: %lldms\n", (long long) timer_uptime_ms());
					b++;
				}
				return 0;
			}

			if (ws_get_flag(ctx, "--system")) {
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
			}

			if (ws_has_arg(ctx, "--set-date-format")) {
				const char* fmt = ws_get_string(ctx, "--set-date-format");
				if (strcmp(fmt, "DMY") == 0 || strcmp(fmt, "dmy") == 0) {
					current_date_format = DD_MM_YYYY;
				} else if (strcmp(fmt, "MDY") == 0 || strcmp(fmt, "mdy") == 0) {
					current_date_format = MM_DD_YYYY;
				} else if (strcmp(fmt, "YMD") == 0 || strcmp(fmt, "ymd") == 0) {
					current_date_format = YYYY_MM_DD;
				} else {
					logger(ERROR, "Wrong argument provided. Run `help time -sdf` to see command usage.");
				}
				return 0;
			}

			if (ws_get_flag(ctx, "--time-format-24")) {
				current_time_format = HOURS_24;
				return 0;
			}

			if (ws_get_flag(ctx, "--time-format-12")) {
				current_time_format = HOURS_12;
				return 0;
			}

			if (ws_get_flag(ctx, "--info")) {
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