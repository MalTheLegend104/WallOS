#include <klibc/kprint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <terminal/commands/system_commands.h>
#include <terminal/terminal.h>

#include <klibc/display.h>
#include <terminal/wall_shell.h>

// A lot of these dont use argc or argv
#pragma GCC diagnostic ignored "-Wunused-parameter"

// ------------------------------------------------------------------------------------------------
// Clear command
// ------------------------------------------------------------------------------------------------
<<<<<<< HEAD
const char* clear_aliases[] = {"clr", "cls"};
int clear_command(void) {
	display_clear();
	display_update_cursor(0, 0);
	// printf("\n");
=======
const char* clear_aliases[] = { "clr", "cls" };
int clear_command(int argc, char** argv) {
	clearVGABuf();
	printf("\n");
	return 0;
}
int clear_help(int argc, char** argv) {
	HelpEntryGeneral entry = {
		"Clear",
		"Clears the screen",
		NULL,
		0,
		clear_aliases,
		2 
	};
	printGeneralHelp(&entry);
	return 0;
}

// ------------------------------------------------------------------------------------------------
// Test command
// ------------------------------------------------------------------------------------------------
const char* test_aliases[] = { "te", "tes", "zest" };
int test_command(int argc, char** argv) {
	printf("argc: %d\n", argc);

	for (int i = 0; i < argc; i++) {
		printf("argv[%d]: %s\n", i, argv[i]);
	}

	return -1; // Success
}
int test_help(int argc, char** argv) {
	if (argc > 1) {
		// Specific Help
		for (int i = 1; i <= argc; i++) {
			// 
			if (strcmp(argv[i], "a") == 0) {
				const char* required[] = {
					"-a     -> desription of the flag -a",
					"-asdf  -> desription of the flag -asdf"
				};
				const char* optional[] = {
					"-d     -> desription of the flag -d",
				};
				HelpEntry entry = {
					"Test A",
					"Test command that does test things.",
					required,
					2,
					optional,
					1
				};
				printSpecificHelp(&entry);
			}
			// Check for other specific commands as you wish
		}

	} else {
		// General Help
		const char* commands[] = {
		"a      -> command a",
		"asdf   -> command asdf"
		};
		HelpEntryGeneral entry = {
			"Test",
			"Test command that does test things.",
			commands,
			2,
			test_aliases,
			2
		};
		printGeneralHelp(&entry);
	}

>>>>>>> origin/main
	return 0;
}

// ------------------------------------------------------------------------------------------------
// Panic Command
// ------------------------------------------------------------------------------------------------
#include <panic.h>

// Original flags never had long forms; --string/--code are new names for -s/-i.
const ws_command_argument_t panic_args[] = {
	{WS_ARG_TYPE_STRING, false, "--string", "-s", "Panics with a custom message. Quote multi-word messages."},
	{WS_ARG_TYPE_INT32, false, "--code", "-i", "Panics with a specific error code."},
};
const size_t panic_args_count = sizeof(panic_args) / sizeof(panic_args[0]);

int panic_command(int argc, char** argv) {
	if (argc > 1) {
		ws_context_t* ctx = ws_getCurrentContext();
		if (ws_parse_args(ctx, argc, argv)) {
			if (ws_has_arg(ctx, "--string")) {
				panic_s(ws_get_string(ctx, "--string"));
			} else if (ws_has_arg(ctx, "--code")) {
				panic_i((int) ws_get_int64(ctx, "--code"));
			}
		}
	}
	panic();
	// This doesn't ever reach here lmfao
	return 0;
}

// ------------------------------------------------------------------------------------------------
// Logo Command
// ------------------------------------------------------------------------------------------------
const ws_command_argument_t logo_args[] = {
	{WS_ARG_TYPE_FLAG, false, "--no-sysinfo", "-nsi", "Skips printing system info under the logo."},
};
const size_t logo_args_count = sizeof(logo_args) / sizeof(logo_args[0]);

int logo_command(int argc, char** argv) {
	bool skip_sysinfo = false;

	if (argc > 1) {
		ws_context_t* ctx = ws_getCurrentContext();
		if (ws_parse_args(ctx, argc, argv)) {
			skip_sysinfo = ws_get_flag(ctx, "--no-sysinfo");
		}
	}

	display_clear();
	print_logo();
	time_command(0, NULL); // Print the time and date beneath the logo
	printf("\n");

	if (!skip_sysinfo) {
		sysinfo_boot();
		printf("\n");
	}

	return 0;
}

extern void register_drive_commands(void);


// Since we dont have malloc, aliases have to be defined outside of context.
// If you try to define it in a function, you'll get a page fault.
void registerSystemCommands() {
	ws_command_t clear_cmd = {0};
	clear_cmd.main_void = clear_command;
	clear_cmd.command_name = "clear";
	clear_cmd.aliases = clear_aliases;
	clear_cmd.alias_count = 2;
	ws_registerCommand(clear_cmd);

	ws_command_t panic_cmd = {0};
	panic_cmd.main_func = panic_command;
	panic_cmd.command_name = "panic";
	panic_cmd.arguments = panic_args;
	panic_cmd.arguments_count = panic_args_count;
	ws_registerCommand(panic_cmd);

	ws_command_t logo_cmd = {0};
	logo_cmd.main_func = logo_command;
	logo_cmd.command_name = "logo";
	logo_cmd.arguments = logo_args;
	logo_cmd.arguments_count = logo_args_count;
	ws_registerCommand(logo_cmd);

	ws_command_t time_cmd = {0};
	time_cmd.main_func = time_command;
	time_cmd.command_name = "time";
	time_cmd.arguments = time_args;
	time_cmd.arguments_count = time_args_count;
	ws_registerCommand(time_cmd);

	ws_command_t meminfo_cmd = {0};
	meminfo_cmd.main_func = meminfo;
	meminfo_cmd.command_name = "meminfo";
	meminfo_cmd.arguments = meminfo_args;
	meminfo_cmd.arguments_count = meminfo_args_count;
	ws_registerCommand(meminfo_cmd);

	ws_command_t sysinfo_cmd = {0};
	sysinfo_cmd.main_void = sysinfo;
	sysinfo_cmd.command_name = "sysinfo";
	ws_registerCommand(sysinfo_cmd);

	// ws_command_t drive_cmd = { 0 };
	// drive_cmd.main_func = drive_command;
	// drive_cmd.command_name = "drive";
	// ws_registerCommand(drive_cmd);
	register_drive_commands();

	ws_command_t shutdown_cmd = {0};
	shutdown_cmd.main_func = shutdown_command;
	shutdown_cmd.command_name = "shutdown";
	shutdown_cmd.arguments = power_state_args;
	shutdown_cmd.arguments_count = power_state_args_count;
	ws_registerCommand(shutdown_cmd);

	ws_command_t reboot_cmd = {0};
	reboot_cmd.main_func = reboot_command;
	reboot_cmd.command_name = "reboot";
	reboot_cmd.arguments = power_state_args;
	reboot_cmd.arguments_count = power_state_args_count;
	ws_registerCommand(reboot_cmd);
}