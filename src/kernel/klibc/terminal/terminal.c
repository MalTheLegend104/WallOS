#include <terminal/terminal.h>
#include <terminal/commands/systemCommands.h>
#include <stdio.h>
#include <string.h>
#include <drivers/keyboard.h>
#include <klibc/kprint.h>
#include <klibc/logger.h>

Command commands[MAX_COMMAND_COUNT];
int currentSpot = 0;
char commandBuf[MAX_COMMAND_BUF];
bool newCommand;

void regiserCommand(const Command c) {
	commands[currentSpot] = c;
	currentSpot++;
}

void deregisterCommand(const Command c) {

}

void helpMain(int argc, char** argv) {
	// Make sure there's more than one argument.
	if (argc > 1) {
		// remove help from argv
		// Shift all pointers one position to the left
		for (int i = 1; i < argc; i++) {
			argv[i - 1] = argv[i];
		}
		argv[argc - 1] = NULL;

		// Update the size of the array
		argc--;

		// Find the command
		for (int i = 0; i < MAX_COMMAND_COUNT; i++) {
			// Check the normal command name
			if (commands[i].commandName && strcmp(commands[i].commandName, argv[0]) == 0) {
				// No help function for command.
				if (!commands[i].helpCommand) {
					set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
					printf("Command \"%s\" does not have a help function.\n", argv[0]);
					set_to_last();
					return;
				}

				// Execute the help command associated with the matched command
				int result = commands[i].helpCommand(argc, argv);
				if (result != 0) {
					// If the command function returns a non-zero value, it may indicate an error
					set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
					printf("Command exited with code: %d\n", result);
					set_to_last();
				}
				return;
			}

			// Check aliases for a match
			for (size_t alias_idx = 0; alias_idx < commands[i].aliases_count; alias_idx++) {
				if (commands[i].aliases[alias_idx] && strcmp(commands[i].aliases[alias_idx], argv[0]) == 0) {
					// No help function for command.
					if (!commands[i].helpCommand) {
						set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
						printf("Command \"%s\" does not have a help function.\n", argv[0]);
						set_to_last();
						return;
					}

					// Execute the help command associated with the matched alias
					int result = commands[i].helpCommand(argc, argv);
					if (result != 0) {
						// If the command function returns a non-zero value, it may indicate an error
						set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
						printf("Command exited with code: %d\n", result);
						set_to_last();
					}
					return;
				}
			}
		}
		// If the command is not found in the registered commands or their aliases
		set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
		printf("Help command not found for: %s\n", argv[0]);
		set_to_last();
		return;
	} else {
		set_colors(VGA_COLOR_CYAN, VGA_DEFAULT_BG);
		printf("Listing all commands:\n");
		printf("To get more info about a command, run `help <command_name>`\n");
		set_to_last();

		set_colors(VGA_COLOR_LIGHT_GREEN, VGA_DEFAULT_BG);
		// List all available commands
		for (int i = 0; i < MAX_COMMAND_COUNT; i++) {
			if (commands[i].commandName) {
				printf("\t%s\n", commands[i].commandName);
			}
		}
		set_to_last();
	}
}

	// Function to execute the registered command based on the input
void executeCommand(char* commandBuf) {
	// Split the commandBuf into arguments based on spaces or other delimiters
	int argc = 0;
	char* argv[MAX_ARGS];

	// Process the commandBuf manually to create argc and argv
	// This should be replaced when we get access to strtok
	int i = 0;
	while (commandBuf[i] != '\0' && argc < MAX_ARGS) {
		// Skip leading spaces or tabs
		while (commandBuf[i] == ' ' || commandBuf[i] == '\t') {
			i++;
		}

		if (commandBuf[i] == '\0') {
			break; // Reached the end of the command buffer
		}

		// Store the start of the argument
		argv[argc] = &commandBuf[i];
		argc++;

		// Find the end of the argument
		while (commandBuf[i] != '\0' && commandBuf[i] != ' ' && commandBuf[i] != '\t' && commandBuf[i] != '\n') {
			i++;
		}

		if (commandBuf[i] == '\0') {
			break; // Reached the end of the command buffer
		}

		// Null-terminate the argument
		commandBuf[i] = '\0';
		i++;
	}

	// Help command
	if (strcmp("help", argv[0]) == 0) {
		helpMain(argc, argv);
		return;
	}

	// Everything that isn't the help command.
	// Check if the given command exists in the registered commands or their aliases
	for (int i = 0; i < MAX_COMMAND_COUNT; i++) {
		if (commands[i].commandName && strcmp(commands[i].commandName, argv[0]) == 0) {
			// Execute the mainCommand function associated with the matched command
			int result = commands[i].mainCommand(argc, argv);
			if (result != 0) {
				// If the command function returns a non-zero value, it may indicate an error
				set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
				printf("Command exited with code: %d\n", result);
				set_to_last();
			}
			return;
		}

		// Check aliases for a match
		for (size_t alias_idx = 0; alias_idx < commands[i].aliases_count; alias_idx++) {
			if (commands[i].aliases[alias_idx] && strcmp(commands[i].aliases[alias_idx], argv[0]) == 0) {
				// Execute the mainCommand function associated with the matched alias
				int result = commands[i].mainCommand(argc, argv);
				if (result != 0) {
					// If the command function returns a non-zero value, it may indicate an error
					set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
					printf("Command exited with code: %d\n", result);
					set_to_last();
				}
				return;
			}
		}
	}

	// If the command is not found in the registered commands or their aliases
	set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
	printf("Command not found: %s\n", argv[0]);
	set_to_last();
}

void terminalMain() {
	registerSystemCommands();
	commandBuf[0] = '\0';
	newCommand = false;
	printf("\n> ");

	while (true) {
		char current = kb_getc();

		if (newCommand) {
			printf("> ");
			newCommand = false;
		}

		// We always want to print the char
		printf("%c", current);

		if (current == '\n') {
			// Execute the command when the user presses enter
			executeCommand(commandBuf);

			// Clear the buffer
			memset(commandBuf, 0, MAX_COMMAND_BUF * sizeof(char));
			commandBuf[0] = '\0';
			newCommand = true;
		} else if (current == '\b') {
			if (strlen(commandBuf) > 0) {
				// Remove the last character by setting it to null terminator
				commandBuf[strlen(commandBuf) - 1] = '\0';
			}
		} else {
			strcat_c(commandBuf, current, MAX_COMMAND_BUF);
		}

		// Eventually we have a shutdown command or smth. 
	}
}

/**
 * @brief Prints the general help info.
 *
 * @param entry pointer to the HelpEntryGeneral that contains the info.
 */
void printGeneralHelp(HelpEntryGeneral* entry) {
	// Command Name
	set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
	if (entry->commandName)
		printf("\n%s\n", entry->commandName);
	set_to_last();

	// Description
	set_colors(VGA_COLOR_CYAN, VGA_DEFAULT_BG);
	if (entry->description)
		printf("%s\n", entry->description);
	set_to_last();

	// Commands
	set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
	printf("\nCommands:\n");
	set_to_last();

	set_colors(VGA_COLOR_GREEN, VGA_DEFAULT_BG);
	if (entry->commands_count > 0) {
		for (int i = 0; i < entry->commands_count; i++) {
			if (entry->commands[i])
				printf("%s\n", entry->commands[i]);
		}
	}
	set_to_last();

	// Aliases
	set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
	printf("\nAliases:\n");
	set_to_last();

	set_colors(VGA_COLOR_GREEN, VGA_DEFAULT_BG);
	if (entry->aliases_count > 0) {
		for (int i = 0; i < entry->aliases_count; i++) {
			if (entry->aliases[i])
				printf("%s\n", entry->aliases[i]);
		}
	}
	set_to_last();
	printf("\n");
}

/**
 * @brief Prints the specific help info.
 *
 * @param entry pointer to the HelpEntry that contains the info.
 */
void printSpecificHelp(HelpEntry* entry) {
	// Command Name
	set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
	if (entry->commandName)
		printf("\n%s\n", entry->commandName);
	set_to_last();

	// Description
	set_colors(VGA_COLOR_CYAN, VGA_DEFAULT_BG);
	if (entry->description)
		printf("%s\n", entry->description);
	set_to_last();

	// Commands
	set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
	printf("Commands:\n");
	set_to_last();

	set_colors(VGA_COLOR_GREEN, VGA_DEFAULT_BG);
	if (entry->required_count > 0) {
		for (int i = 0; i < entry->required_count; i++) {
			if (entry->required[i])
				printf("%s\n", entry->required[i]);
		}
	}
	set_to_last();

	// Aliases
	set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
	printf("\nAliases:\n");
	set_to_last();

	set_colors(VGA_COLOR_GREEN, VGA_DEFAULT_BG);
	if (entry->optional_count > 0) {
		for (int i = 0; i < entry->optional_count; i++) {
			if (entry->optional[i])
				printf("%s\n", entry->optional[i]);
		}
	}
	set_to_last();
	printf("\n");
}