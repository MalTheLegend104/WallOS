#include <terminal/terminal.h>
#include <terminal/commands/systemCommands.h>
#include <stdio.h>
#include <klibc/kprint.h>
#include <stdlib.h>

const char* clear_aliases[] = { "clr" };
int clear_command(int argc, char** argv) {
	clearVGABuf();
	// Clearing does wierd things.
	printf(" ");
	return 0;
}

const char* test_aliases[] = { "te", "tes" };
int test_command(int argc, char** argv) {
	printf("argc: %d\n", argc);

	for (int i = 0; i < argc; i++) {
		printf("argv[%d]: %s\n", i, argv[i]);
	}

	return -1; // Success
}
#include <string.h>
int testHelp(int argc, char** argv) {
	if (argc > 1) {
		// Specific Help
		for (int i = 1; i <= argc; i++) {
			// 
			if (strcmp(argv[i], "a") == 0) {
				char* required[] = {
					"-a     -> desription of the flag -a",
					"-asdf  -> desription of the flag -asdf"
				};
				char* optional[] = {
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
		char* commands[] = {
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

	return 0;
}

// Since we dont have malloc, aliases have to be defined outside of context.
// If you try to define it in a function, you'll get a page fault.
void registerSystemCommands() {
	regiserCommand((Command) { test_command, testHelp, "test", test_aliases, 2 });
	regiserCommand((Command) { clear_command, NULL, "clear", clear_aliases, 1 });


}