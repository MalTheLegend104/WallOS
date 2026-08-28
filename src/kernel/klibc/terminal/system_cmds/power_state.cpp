#include <terminal/commands/system_commands.h>
#include <acpi/acpi_api.h>

#include <string.h>
#include <stdlib.h>
#include <system/timer.h>
#include <terminal/wall_shell.h>

extern "C" const ws_command_argument_t power_state_args[] = {
	{ WS_ARG_TYPE_GENERIC, false, "time", NULL, "\"now\", or the amount of time to wait before acting." },
};
extern "C" const size_t power_state_args_count = sizeof(power_state_args) / sizeof(power_state_args[0]);

int shutdown_command(int argc, char** argv) {
	// Time in seconds
	uint32_t shutdown_time = 0;

	if (argc > 1) {
		ws_context_t* ctx = ws_getCurrentContext();
		if (ws_parse_args(ctx, argc, argv) && ws_has_arg(ctx, "time")) {
			const char* time_arg = ws_get_generic(ctx, "time");
			if (strcmp(time_arg, "now") != 0) {
				uint32_t temp = atoi(time_arg);
				if (temp > 0) shutdown_time = temp;
			}
		}
	}

	if (shutdown_time > 0) {
		busy_wait_ms(shutdown_time);
	}

	acpi_shutdown();
}

int reboot_command(int argc, char** argv) {
	uint32_t reboot_time = 0;

	if (argc > 1) {
		ws_context_t* ctx = ws_getCurrentContext();
		if (ws_parse_args(ctx, argc, argv) && ws_has_arg(ctx, "time")) {
			const char* time_arg = ws_get_generic(ctx, "time");
			if (strcmp(time_arg, "now") != 0) {
				uint32_t temp = atoi(time_arg);
				if (temp > 0) reboot_time = temp;
			}
		}
	}

	if (reboot_time > 0) {
		busy_wait_ms(reboot_time);
	}

	acpi_reboot();
}