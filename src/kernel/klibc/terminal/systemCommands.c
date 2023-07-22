#include <terminal/terminal.h>
#include <terminal/commands/systemCommands.h>
#include <stdio.h>
#include <klibc/kprint.h>
#include <stdlib.h>
#include <string.h>

// A lot of these dont use argc or argv
#pragma GCC diagnostic ignored "-Wunused-parameter" 

// ------------------------------------------------------------------------------------------------
// Clear command
// ------------------------------------------------------------------------------------------------
const char* clear_aliases[] = { "clr" };
int clear_command(int argc, char** argv) {
	clearVGABuf();
	// Clearing does wierd things.
	printf(" ");
	return 0;
}

// ------------------------------------------------------------------------------------------------
// Test command
// ------------------------------------------------------------------------------------------------

const char* test_aliases[] = { "te", "tes" };
int test_command(int argc, char** argv) {
	printf("argc: %d\n", argc);

	for (int i = 0; i < argc; i++) {
		printf("argv[%d]: %s\n", i, argv[i]);
	}

	return -1; // Success
}
int testHelp(int argc, char** argv) {
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

	return 0;
}

// ------------------------------------------------------------------------------------------------
// Panic Command
// ------------------------------------------------------------------------------------------------
#include <panic.h>
void concatStrings(char** arr, int start, int end, char* result, int len) {
	if (arr == NULL || result == NULL || start < 0 || end < 0 || end < start) {
		result[0] = '\0'; // Return an empty string for invalid input
		return;
	}

	int currentPos = 0;
	int bufferLength = len;

	// Copy the individual strings into the concatenated string
	for (int i = start; i <= end; i++) {
		int strLength = strlen(arr[i]);
		if (currentPos + strLength >= bufferLength - 1) {
			break; // Buffer full, stop concatenating
		}

		memcpy(result + currentPos, arr[i], strLength);
		currentPos += strLength;
		if (i < end) {
			result[currentPos] = ' ';
			currentPos++;
		}
	}

	result[currentPos] = '\0'; // Null-terminate the concatenated string
}

int panic_command(int argc, char** argv) {
	if (argc > 1) {
		char buf[128];
		concatStrings(argv, 1, argc - 1, buf, 128);
		panic_s(buf);
	} else {
		panic();
	}
	// This doesn't ever reach here lmfao
	return 0;
}


// Since we dont have malloc, aliases have to be defined outside of context.
// If you try to define it in a function, you'll get a page fault.
void registerSystemCommands() {
	regiserCommand((Command) { test_command, testHelp, "test", test_aliases, 2 });
	regiserCommand((Command) { clear_command, NULL, "clear", clear_aliases, 1 });
	regiserCommand((Command) { panic_command, NULL, "panic", NULL, 0 });

}