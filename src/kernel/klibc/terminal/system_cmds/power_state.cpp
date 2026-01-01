#include <terminal/commands/system_commands.h>
#include <acpi/acpi_api.h>

#include <string.h>
#include <stdlib.h>
#include <system/timing.h>

int shutdown_command(int argc, char** argv) {
	// Time in seconds
	uint32_t shutdown_time = 0;

	if (argc > 1) {
		if (strcmp(argv[1], "now") == 0) {
			shutdown_time = 0;
		} else {
			uint32_t temp = atoi(argv[1]);
			if (temp > 0) shutdown_time = temp;
		}
	}

	if (shutdown_time > 0) {
		sleep(shutdown_time);
	}

	acpi_shutdown();
}

int reboot_command(int argc, char** argv) {
	uint32_t reboot_time = 0;

	if (argc > 1) {
		if (strcmp(argv[1], "now") == 0) {
			reboot_time = 0;
		} else {
			uint32_t temp = atoi(argv[1]);
			if (temp > 0) reboot_time = temp;
		}
	}

	if (reboot_time > 0) {
		sleep(reboot_time);
	}

	acpi_reboot();
}