#include <terminal/terminal.h>
#include <terminal/commands/systemCommands.h>
#include <stdio.h>
#include <string.h>
#include <timing.h>
#include <drivers/keyboard.h>
#include <klibc/kprint.h>
#include <klibc/logger.h>

char previousCommands[PREVIOUS_COMMAND_BUF_SIZE][MAX_COMMAND_BUF];
size_t previous_commands_size = 0;
Command commands[MAX_COMMAND_COUNT];
int currentSpot = 0;
char commandBuf[MAX_COMMAND_BUF];
bool newCommand;

void registerCommand(const Command c) {
	commands[currentSpot] = c;
	currentSpot++;
}

/**
 * @brief Operator overloading isn't available in C. Compare two commands using this.
 *
 * @param c1 Command to compare.
 * @param c2 Command to compare.
 * @return true If they are the same.
 * @return false If they are different.
 */
bool compareCommands(const Command c1, const Command c2) {
	if (c1.aliases != c2.aliases) return false;
	if (c1.aliases_count != c2.aliases_count) return false;
	if (strcmp(c1.commandName, c2.commandName) != 0) return false;
	if (c1.helpCommand != c2.helpCommand) return false;
	if (c1.mainCommand != c2.mainCommand) return false;
	return true;
}

/**
 * @brief Deregister a command from the terminal. Does nothing if the command cannot be found.
 *
 * @param c Command to be deregistered.
 */
void deregisterCommand(const Command c) {
	for (int i = 0; i < currentSpot; i++) {
		if (compareCommands(commands[i], c)) {
			// actually deregister them here
			// im too lazy to care
		}
	}
}

bool startsWith(const char* str, const char* prefix) {
	while (*prefix) {
		if (*prefix != *str) {
			return false;
		}
		prefix++;
		str++;
	}
	return true;
}

void helpSearch(char* str) {
	set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
	printf("List of commands starting with \"%s\":\n", str);
	set_to_last();
	for (int i = 0; i < MAX_COMMAND_COUNT; i++) {
		set_colors(VGA_COLOR_LIGHT_GREEN, VGA_DEFAULT_BG);
		if (commands[i].commandName && startsWith(commands[i].commandName, str)) {
			printf("\t%s\n", commands[i].commandName);
		}

		// Check aliases for a match
		for (size_t alias_idx = 0; alias_idx < commands[i].aliases_count; alias_idx++) {
			if (commands[i].aliases[alias_idx] && startsWith(commands[i].aliases[alias_idx], str)) {
				printf("\t%s (A)\n", commands[i].aliases[alias_idx]);
			}
		}
		set_to_last();
	}
}
int helpHelp(int argc, char** argv) {
	const char* optional[] = {
				"-s <string> -> Lists all commands and aliases that start with <string>."
	};
	HelpEntry entry = {
		"Help",
		"The help menu.",
		NULL,
		0,
		optional,
		1
	};
	printSpecificHelp(&entry);
	return 0;
}
int helpMain(int argc, char** argv) {
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
		// Check for more args
		if (argc > 1) {
			if ((strcmp(argv[0], "-s") == 0) || (strcmp(argv[0], "-search") == 0)) {
				if (argc == 1) {
					set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
					printf("Search flag must be followed by an arguement.\n");
					set_to_last();
				} else {
					helpSearch(argv[1]);
					return 0;
				}
			}
		}

		// Find the command
		for (int i = 0; i < MAX_COMMAND_COUNT; i++) {
			// Check the normal command name
			if (commands[i].commandName && strcmp(commands[i].commandName, argv[0]) == 0) {
				// No help function for command.
				if (!commands[i].helpCommand) {
					set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
					printf("Command \"%s\" does not have a help function.\n", argv[0]);
					set_to_last();
					return 0;
				}

				// Execute the help command associated with the matched command
				int result = commands[i].helpCommand(argc, argv);
				if (result != 0) {
					// If the command function returns a non-zero value, it may indicate an error
					set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
					printf("Command exited with code: %d\n", result);
					set_to_last();
				}
				return 0;
			}

			// Check aliases for a match
			for (size_t alias_idx = 0; alias_idx < commands[i].aliases_count; alias_idx++) {
				if (commands[i].aliases[alias_idx] && strcmp(commands[i].aliases[alias_idx], argv[0]) == 0) {
					// No help function for command.
					if (!commands[i].helpCommand) {
						set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
						printf("Command \"%s\" does not have a help function.\n", argv[0]);
						set_to_last();
						return 0;
					}

					// Execute the help command associated with the matched alias
					int result = commands[i].helpCommand(argc, argv);
					if (result != 0) {
						// If the command function returns a non-zero value, it may indicate an error
						set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
						printf("Command exited with code: %d\n", result);
						set_to_last();
					}
					return 0;
				}
			}
		}
		// If the command is not found in the registered commands or their aliases
		set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
		printf("Help command not found for: %s\n", argv[0]);
		set_to_last();
		return 0;
	} else {
		printf("\n");
		set_colors(VGA_COLOR_CYAN, VGA_DEFAULT_BG);
		printf("To get more info about a command, run `help <command_name>`\n");
		set_to_last();
		set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
		printf("All commands:\n");
		set_to_last();

		set_colors(VGA_COLOR_LIGHT_GREEN, VGA_DEFAULT_BG);
		// List all available commands
		for (int i = 0; i < MAX_COMMAND_COUNT; i++) {
			if (commands[i].commandName) {
				printf("  %s\n", commands[i].commandName);
			}
		}
		set_to_last();
		printf("\n");
	}
	return 0;
}

const char* historyAliases[] = { "hist" };
int historyHelp(int argc, char** argv) {
	HelpEntryGeneral entry = {
		"History",
		"Displays the terminal history. Limit of 32 previous commands.",
		NULL,
		0,
		historyAliases,
		1
	};
	printGeneralHelp(&entry);
	return 0;
}

int historyCommand(int argc, char** argv) {
	set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
	for (size_t i = 0; i < previous_commands_size; i++) {
		printf("%s\n", previousCommands[i]);
	}
	set_to_last();
	return 0;
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
	printf("Command not found: \"%s\"\n", argv[0]);
	set_to_last();
}

void terminalMain() {
	registerSystemCommands();
	set_colors(VGA_COLOR_PINK, VGA_DEFAULT_BG);
	printf("Initalizing Terminal...");
	set_to_last();
	// Help and History have to be defined in this file.
	registerCommand((Command) { helpMain, helpHelp, "help", 0, 0 });
	registerCommand((Command) { historyCommand, historyHelp, "history", historyAliases, 1 });
	sleep(1500);
	executeCommand("logo");  // This is where the cursor first gets enabled

	commandBuf[0] = '\0';
	size_t position_in_previous = 0;
	size_t position_in_current = 0;

	newCommand = false;
	bool tab_pressed;

	// Actually do command stuff now.
	printf("\n> ");
	while (true) {
		char current = kb_getc();
		KeyboardState state = getKeyboardState();

		if (newCommand) {
			printf("> ");
			newCommand = false;
			tab_pressed = false;
			position_in_previous = 0;
		}

		if (state.escaped) {
			switch (state.last_scancode) {
				case SC_KEYPAD_2: // Down
					if (position_in_previous > 0) {
						position_in_previous--;
						clear_current_row();
						printf("\r > %s", previousCommands[position_in_previous]);
						memset(commandBuf, 0, MAX_COMMAND_BUF);
						memcpy(commandBuf, previousCommands[position_in_previous], strlen(previousCommands[position_in_previous]));
					}
					continue;
				case SC_KEYPAD_4: // Left
					// Implement moving left across current command
					continue;
				case SC_KEYPAD_8: // Up
					clear_current_row();
					printf("\r > %s", previousCommands[position_in_previous]);


					memset(commandBuf, 0, MAX_COMMAND_BUF);
					memcpy(commandBuf, previousCommands[position_in_previous], strlen(previousCommands[position_in_previous]));
					if (previous_commands_size > 0 && position_in_previous < previous_commands_size - 1) {
						position_in_previous++;
					}
					continue;
				case SC_KEYPAD_6: // Right
					// Implement moving right across current command
					continue;
				default:
					break;
			}
		}

		// We always want to print the char, unless it's backspace or tab
		// Backspace makes sure we cant delete past the beginning of the line.
		if (current == '\n') {
			printf("%c", current);
			// If there's an empty command we just start a new line.
			if (strlen(commandBuf) == 0) {
				newCommand = true;
				continue;
			}

			// Move everything right in the previous buf
			if (previous_commands_size > 0) {
				if (strcmp(previousCommands[0], commandBuf) != 0) {
					for (size_t i = previous_commands_size; i > 0; i--) {
						memcpy(previousCommands[i], previousCommands[i - 1], strlen(previousCommands[i - 1]));
						memset(previousCommands[i - 1], 0, MAX_COMMAND_BUF);
					}

					if (previous_commands_size < PREVIOUS_COMMAND_BUF_SIZE) {
						previous_commands_size++;
					}
				}
				memcpy(previousCommands[0], commandBuf, strlen(commandBuf));
			} else {
				previous_commands_size++;
				memcpy(previousCommands[0], commandBuf, strlen(commandBuf));
			}

			// Execute the command when the user presses enter
			executeCommand(commandBuf);

			// Clear the buffer
			memset(commandBuf, 0, MAX_COMMAND_BUF * sizeof(char));
			commandBuf[0] = '\0';
			newCommand = true;
		} else if (current == '\b') {
			if (strlen(commandBuf) > 0) {
				// Remove the last character by setting it to null terminator
				printf("%c", current);
				commandBuf[strlen(commandBuf) - 1] = '\0';
			}
		} else if (current == '\t') {
			// See if we can autocomplete a command
			const char* list[50]; // List of current possible commands
			int list_size = 0;
			for (int i = 0; i < MAX_COMMAND_COUNT; i++) {
				set_colors(VGA_COLOR_LIGHT_GREEN, VGA_DEFAULT_BG);
				if (commands[i].commandName && startsWith(commands[i].commandName, commandBuf)) {
					list[list_size] = commands[i].commandName;
					list_size++;
				}

				// Check aliases for a match
				for (size_t alias_idx = 0; alias_idx < commands[i].aliases_count; alias_idx++) {
					if (commands[i].aliases[alias_idx] && startsWith(commands[i].aliases[alias_idx], commandBuf)) {
						// If it's an alias of a command that's already in the list, we dont want it.
						bool already_in_list = false;
						for (int j = 0; j < list_size; j++) {
							if (strcmp(list[j], commands[i].commandName) == 0) {
								already_in_list = true;
								break;
							}
						}
						if (!already_in_list) {
							list[list_size] = commands[i].aliases[alias_idx];
							list_size++;
						}
					}
				}
				set_to_last();
			}
			if (list_size == 1) {
				// Print the rest of the command
				size_t len = strlen(commandBuf);
				const char* currentCommand = list[0];
				for (size_t i = len; i < strlen(currentCommand); i++) {
					printf("%c", currentCommand[i]);
					strcat_c(commandBuf, currentCommand[i], MAX_COMMAND_BUF);
				}
				tab_pressed = false;
			} else if (tab_pressed) {
				if (list_size == 0) {
					set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
					printf("\nNo command starting with: %s\n", commandBuf);
					set_to_last();
					// Clear the buffer
					memset(commandBuf, 0, MAX_COMMAND_BUF * sizeof(char));
					commandBuf[0] = '\0';
					newCommand = true;
				} else if (list_size > 1) {
					// Print out all commands
					set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
					printf("\n");
					for (int i = 0; i < list_size; i++) {
						printf("%s\n", list[i]);
					}
					set_to_last();
					// Reprint the command line
					printf("> %s", commandBuf);
				}
				tab_pressed = false;
			} else {
				tab_pressed = true;
			}
		} else {
			printf("%c", current);
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
	if (entry->commands_count > 0) {
		set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
		printf("\nCommands:\n");
		set_to_last();

		set_colors(VGA_COLOR_GREEN, VGA_DEFAULT_BG);
		for (int i = 0; i < entry->commands_count; i++) {
			if (entry->commands[i])
				printf("  %s\n", entry->commands[i]);
		}
		set_to_last();
	}

	// Aliases
	if (entry->aliases_count > 0) {
		set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
		printf("\nAliases:\n");
		set_to_last();

		set_colors(VGA_COLOR_GREEN, VGA_DEFAULT_BG);
		for (int i = 0; i < entry->aliases_count; i++) {
			if (entry->aliases[i])
				printf("  %s\n", entry->aliases[i]);
		}
		set_to_last();
	}
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
	if (entry->required_count > 0) {
		set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
		printf("Required Flags:\n");
		set_to_last();

		set_colors(VGA_COLOR_GREEN, VGA_DEFAULT_BG);
		for (int i = 0; i < entry->required_count; i++) {
			if (entry->required[i])
				printf("  %s\n", entry->required[i]);
		}
		set_to_last();
	}

	// Aliases
	if (entry->optional_count > 0) {
		set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
		printf("\nOptional Flags:\n");
		set_to_last();

		set_colors(VGA_COLOR_GREEN, VGA_DEFAULT_BG);
		for (int i = 0; i < entry->optional_count; i++) {
			if (entry->optional[i])
				printf("  %s\n", entry->optional[i]);
		}
		set_to_last();
	}
	printf("\n");
}