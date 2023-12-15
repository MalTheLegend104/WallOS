#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>
#include <stddef.h>

#define MAX_COMMAND_BUF 	256
#define MAX_COMMAND_COUNT	32
#define MAX_ARGS 			32
#define PREVIOUS_COMMAND_BUF_SIZE 32

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct {
		int (*mainCommand)(int argc, char** argv);
		int (*helpCommand)(int argc, char** argv);
		const char* commandName;
		const char** aliases;
		size_t aliases_count;
	} Command;


	typedef struct {
		const char* commandName;
		const char* description;
		const char** commands;
		const int commands_count;
		const char** aliases;
		const int aliases_count;
	} HelpEntryGeneral;

	typedef struct {
		const char* commandName;
		const char* description;
		const char** required;
		const int required_count;
		const char** optional;
		const int optional_count;
	} HelpEntry;

	void registerCommand(const Command c);
	void deregisterCommand(const Command c);
	void executeCommand(char* commandBuf);
	void terminalMain();

	void printGeneralHelp(HelpEntryGeneral* entry);
	void printSpecificHelp(HelpEntry* entry);

#ifdef __cplusplus
}
#endif
#endif //TERMINAL_H