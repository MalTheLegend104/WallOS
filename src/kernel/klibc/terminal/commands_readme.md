# Terminal Commands

This command handler is **very** verstaile, as long as you treat it well. There are few conventions about this we need to go over.

### Anything can be a command.

- Any function that takes in argc and argv, and returns an int, can be a command.

### It is written entirely in C.

- It expects C. You ***can*** write C++ that works with it, but you have to be careful to deal with the structs properly.
- C++ code is recommended to have a wrapper C function that handles the structs, to avoid C++ treating them as classes.

### "Counts" are very important.

- For any field in a struct that is a "count", like `aliases_count`, are expected to behave like `strlen`. It's the total count, not the indexes.
- We will attempt to access any part of the array as long as our "count" says that it's there. If count is wrong, it's unexpected behavior (although it'll probably cause an interrupt of some type).

### Char** MUST be declared outside of context.

* Until we have `malloc` and `free`, we cannot declare `char**` in context. It will get destroyed when the function declaring them exits, causing a page fault when trying to access them. If you declare them outside the scope of a function, it will behave as expected.

  > The exception to this is `help` commands. If you create `HelpEntry`'s inside the same scope that you print them, they will behave properly. This mostly applies to aliases.
  >


## Example of a perfect setup

This command can be called using `example`, `ex`, or `exam`, and `help example` (or any of it's aliases), will display the help message. Running `help example a` will display the specific help message for `example a`.

```c
const char* example_aliases[] = { "ex", "exam" };
int example_command(int argc, char** argv) {
	// Print argc
	printf("argc: %d\n", argc);

	// Print argc
	for (int i = 0; i < argc; i++) {
		printf("argv[%d]: %s\n", i, argv[i]);
	}

	return 0; // Success
}

int example_help(int argc, char** argv) {
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
					"Example A",
					"Description of the command.",
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
			"a      -> <description of command a>",
			"asdf   -> <description of command a>"
		};

		HelpEntryGeneral entry = {
			"Example",
			"Example description.",
			commands,
			2,
			example_aliases,
			2
		};

		printGeneralHelp(&entry);
	}

	return 0;
}
```
