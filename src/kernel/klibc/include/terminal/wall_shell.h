/**
 * @file wall_shell.h
 * @author MalTheLegend104
 * @brief Main header file for WallShell.
 *
 * C99 compliant command handler. Meant to be easily portable and highly configurable.
 *
 * @version v2.0.0
 * @copyright
 * Copyright 2026 MalTheLegend104
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

/* Freestanding headers. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

/* Standard Library Headers */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* Config Header */
#if defined(__has_include)
#if __has_include("ws_config.h")
#include "ws_config.h"
#endif
#else
#include "ws_config.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif


/*********************************************************************************
 * Check for user definitions
 *********************************************************************************/
#ifndef PREVIOUS_BUF_SIZE
#define PREVIOUS_BUF_SIZE 50
#endif // PREVIOUS_BUF_SIZE

#ifndef MAX_COMMAND_BUF
#define MAX_COMMAND_BUF 256
#endif // MAX_COMMAND_BUF

#ifdef THREADED_SUPPORT
#ifdef DISABLE_MALLOC
#error "Threaded support can't exist without malloc."
#endif

#ifndef CUSTOM_THREADS
#ifdef _WIN32
#include <Windows.h>
/**
 * @brief Wrapper around your system's mutex type.
 * @note `CRITICAL_SECTION` is replaced with your systems mutex type.
 */
	typedef CRITICAL_SECTION ws_mutex_t;
	/**
	 * @brief Wrapper around your system's thread handle.
	 * @note `DWORD` is replaced with your systems thread handle type.
	 */
	typedef DWORD ws_thread_id_t;

#else
#include <pthread.h>
/**
 * @brief Wrapper around your system's mutex type.
 * @note `pthread_mutex_t` is replaced with your systems mutex type.
 */
	typedef pthread_mutex_t ws_mutex_t;
	/**
	 * @brief Wrapper around your system's thread hadnle.
	 * @note `uint64_t` is replaced with your systems thread handle type.
	 */
	typedef uint64_t ws_thread_id_t;
#endif // _WIN32
#endif
/* Mutex */
	void ws_lockMutex(ws_mutex_t* mut);
	void ws_unlockMutex(ws_mutex_t* mut);
	ws_mutex_t* ws_createMutex();
	void ws_destroyMutex(ws_mutex_t* mut);

	/* Thread ID */
	ws_thread_id_t ws_getThreadID();

	/* Atomic Bool */
	typedef struct {
		bool b;
		ws_mutex_t* mut;
	} ws_atomic_bool_t;

	bool ws_getAtomicBool(ws_atomic_bool_t* ab);
	void ws_setAtomicBool(ws_atomic_bool_t* ab, bool b);
	ws_atomic_bool_t* ws_createAtomicBool(bool b);
	void ws_destroyAtomicBool(ws_atomic_bool_t* ab);

	void ws_sleep(size_t ms);

	void ws_stopTerminal();

	/* Thread names for logging */
#ifndef NO_WS_LOGGING
	void ws_setThreadName(char* name);
	void ws_removeThreadName(const char* name);
	void ws_printThreadID();
	void ws_doPrintThreadID(bool b);
#endif // NO_WS_LOGGING
#endif // THREADED_SUPPORT

#ifdef DISABLE_MALLOC
#ifndef COMMAND_LIMIT
#define COMMAND_LIMIT 25
#endif
#ifndef MAX_ARGS
#define MAX_ARGS 32
#endif
#ifndef MAX_ARGPARSE_NODES
/**
 * @brief Max number of parsed argparse entries a single ws_context_t can hold when DISABLE_MALLOC is defined.
 *
 * Since there's no malloc available, the parsed argument list backing ws_context_t is a static array instead of a linked list.
 * This defines the size of that array.
 * If a command has more arguments than this (after parsing), ws_parse_args() will fail with an error.
 */
#define MAX_ARGPARSE_NODES 32
#endif
#endif // DISABLE_MALLOC

	typedef enum {
		WS_NO_ERROR = 0,
		WS_OUT_OF_MEMORY,
		WS_COMMAND_LIMIT_REACHED,
		WS_OUT_STREAM_NOT_SET,
		WS_WS_SETUP_ERROR,
		WS_UNBALANCED_QUOTES //< The arguments to the function have an uneven amount of double quotes.
	} ws_error_t;

	// Opaque handle for parsed command execution context
	typedef struct ws_context ws_context_t;

	typedef int (*ws_command_func_main_void_t)(void);          // int func(void);
	typedef int (*ws_command_func_main_t)(int, char**);        // int func(int, char**);
	typedef int (*ws_command_func_env_t)(int, char**, char**); // int func(int, char**, char**);
	typedef void (*ws_command_func_help_t)(int, char**);       // void func(int, char**);

	typedef enum {
		WS_ARG_TYPE_GENERIC,

		WS_ARG_TYPE_FLAG,
		WS_ARG_TYPE_BOOL,

		WS_ARG_TYPE_CHAR,
		WS_ARG_TYPE_STRING, // If you need a path, you should get the path as a string via this arg, and open the file yourself

		WS_ARG_TYPE_INT8,
		WS_ARG_TYPE_INT16,
		WS_ARG_TYPE_INT32,
		WS_ARG_TYPE_INT64,

		WS_ARG_TYPE_UINT8,
		WS_ARG_TYPE_UINT16,
		WS_ARG_TYPE_UINT32,
		WS_ARG_TYPE_UINT64,

		WS_ARG_TYPE_FLOAT,
		WS_ARG_TYPE_DOUBLE,
	} ws_argument_type_t;

	typedef struct ws_command_argument {
		/* If the type is FLAG or BOOL, this acts as a "toggle", with no expected following argument.
		 * If the type is anything else, it is expected to be followed by the correct type.
		 * Type "BOOL" has an optional "t/f/true/false" following arg, FLAG does not.
		 *
		 * Type GENERIC is special. It is used to say that there is no preceding argument.
		 * For example, say we have a "build" command, that takes a target name as a GENERIC:
		 * `build <target>`. The "target" argument would be GENERIC.
		 *
		 * GENERICS are expected to be in a constant order.
		 * Say a command needs a "major.minor.patch" input, and wants to use generics:
		 * `cmd major minor patch`, would require that the argument array for the ws_command_t to have them in order in the list.
		 */
		ws_argument_type_t type;

		bool required; // Required. Set to true if this argument is a required. False otherwise.

		/* Required.
		 * This is the name of the argument.
		 * If not GENERIC, it is the "--" form of the argument.
		 * You can add the `--` if you want, wallshell will add it itself otherwise.
		 *
		 * This will be used for matching in ws_get<type>() commands.
		 * For example, if `this->name = "--arg1"` (and type is UINT64), a call to `ws_get_uint64("arg1")` or `ws_get_uint64("--arg1")` would work.
		 */
		const char* name;
		const char* shortform; // Optional. For example, `--help` could have `-h` as a shortform. Same as above, you can add the `-` if you want, otherwise wallshell will add it during parsing

		const char* description; // Optional. This is used in the `help` output.
	} ws_command_argument_t;

	typedef struct {
		// Required.
		const char* command_name;
		// No, I don't like the const char* const* either.
		// It's technically more correct than const char**
		const char* const* aliases; // optional
		uint8_t	alias_count; // required if aliases is defined.

		/* One of main_void, main_func, or env_func is required.
		 * If all are defined, only env_func will be called.
		 * The most specific defined function is called, in order from least specific to most:
		 * - main void
		 * - main
		 * - main env
		 */
		ws_command_func_main_void_t main_void;
		ws_command_func_main_t main_func;
		ws_command_func_env_t env_func;

		/* Optional
		 * Wallshell will call this in these instances:
		 * - `help <command>`
		 * - `<command> --help`
		 * - `<command> -h`
		 * Use the standard help interface for this to ensure consistency.
		 */
		ws_command_func_help_t help_func;

		/* Optional
		 * If the major/minor/patch fields are defined, WallShell will display them in `ws_info` calls.
		 * If unused, set all to zero.
		 */
		uint8_t major;
		uint8_t minor;
		uint8_t patch;

		/* Optional
		 * Array of all arguments belonging to this command.
		 */
		const ws_command_argument_t* arguments;
		size_t arguments_count;
	} ws_command_t;

	/**
	 * Parse and validate the arguments for the current command.
	 *
	 * This function parses argc/argv according to the argument definitions and stores the resulting values in ctx for subsequent ws_get<type>() calls.
	 *
	 * All argument validation is performed by this function, including:
	 * - Required arguments being present.
	 * - Arguments having the expected type.
	 * - Values being valid for their declared type.
	 * - Named arguments matching a defined argument or alias.
	 * - GENERIC arguments appearing in their defined order.
	 * - Arguments that do not accept a value not being followed by one.
	 *
	 * If an argument is invalid, an appropriate error is written to WallShell's designated output (ws_setStream()) and this function returns false.
	 * The command can continue execution if this is returned false, but it should not make any assumptions about the arguments.
	 *
	 * On success, the parsed and converted values are stored in ctx and may be retrieved using ws_has_arg() and the ws_get<type>() functions.
	 *
	 * This function should be called exactly once for a command invocation, before any ws_has_arg() or ws_get<type>() calls.
	 * It should normally only  be used by commands receiving argc/argv.
	 * `int func(void)` commands have no arguments to parse unless they are explicitly supplied with an argument vector by the caller.
	 *
	 * @param ctx  Command execution context used to store the parsed arguments.
	 * @param argc Number of arguments in argv.
	 * @param argv Argument vector to parse.
	 *
	 * @retval true if all arguments are valid and were successfully parsed.
	 * @retval false if an argument is invalid or required validation fails.
	 */
	bool ws_parse_args(ws_context_t* ctx, int argc, char** argv);
	bool ws_has_arg(const ws_context_t* ctx, const char* name);

	bool ws_get_flag(const ws_context_t* ctx, const char* name);
	bool ws_get_bool(const ws_context_t* ctx, const char* name);
	char ws_get_char(const ws_context_t* ctx, const char* name);
	const char* ws_get_string(const ws_context_t* ctx, const char* name);
	int64_t ws_get_int64(const ws_context_t* ctx, const char* name);
	uint64_t ws_get_uint64(const ws_context_t* ctx, const char* name);
	double ws_get_double(const ws_context_t* ctx, const char* name);
	const char* ws_get_generic(const ws_context_t* ctx, const char* name);

	/* There are more types than we have ws_get_* functions for.
	 * Any of the other int types should be cast to the proper type.
	 * This is merely to avoid having a ton a get functions that essentially do the same thing.
	 */

	/**
	 * @brief Returns the argparse context tied to the command WallShell is currently dispatching.
	 *
	 * WallShell sets this up internally immediately before invoking a command's main_func/env_func, and tears it down immediately after that call returns.
	 * This means it should only be called from within the body (or something called directly by the body) of a command's main_func/env_func, and its result should not be stored past the lifetime of that call.
	 *
	 * `main_void` commands are never given any arguments, so this context is not meaningful for them.
	 *
	 * @return ws_context_t* Context for the currently executing command, or NULL if no command is currently being dispatched.
	 */
	ws_context_t* ws_getCurrentContext();

	/**
	 * @brief Prints a consistent, auto-generated help entry for a command based on its registered ws_command_argument_t list.
	 *
	 * This is what WallShell calls itself when a command doesn't define a help_func.
	 * If you do define a help_func, you can call this from inside of it to keep your command's help output consistent with the rest of WallShell before/after adding your own extra information.
	 *
	 * @param command Command to print help for.
	 */
	void ws_printCommandHelp(const ws_command_t* command);

	typedef enum {
		WS_FG_DEFAULT = 0,
		WS_FG_BLACK = 30,
		WS_FG_RED = 31,
		WS_FG_GREEN = 32,
		WS_FG_YELLOW = 33,
		WS_FG_BLUE = 34,
		WS_FG_MAGENTA = 35,
		WS_FG_CYAN = 36,
		WS_FG_WHITE = 37,

		WS_FG_BRIGHT_BLACK = 90,
		WS_FG_BRIGHT_RED = 91,
		WS_FG_BRIGHT_GREEN = 92,
		WS_FG_BRIGHT_YELLOW = 93,
		WS_FG_BRIGHT_BLUE = 94,
		WS_FG_BRIGHT_MAGENTA = 95,
		WS_FG_BRIGHT_CYAN = 96,
		WS_FG_BRIGHT_WHITE = 97,
	} ws_fg_color_t;

	typedef enum {
		WS_BG_DEFAULT = 0,
		WS_BG_BLACK = 40,
		WS_BG_RED = 41,
		WS_BG_GREEN = 42,
		WS_BG_YELLOW = 43,
		WS_BG_BLUE = 44,
		WS_BG_MAGENTA = 45,
		WS_BG_CYAN = 46,
		WS_BG_WHITE = 47,

		WS_BG_BRIGHT_BLACK = 100,
		WS_BG_BRIGHT_RED = 101,
		WS_BG_BRIGHT_GREEN = 102,
		WS_BG_BRIGHT_YELLOW = 103,
		WS_BG_BRIGHT_BLUE = 104,
		WS_BG_BRIGHT_MAGENTA = 105,
		WS_BG_BRIGHT_CYAN = 106,
		WS_BG_BRIGHT_WHITE = 107,
	} ws_bg_color_t;

	typedef struct {
		ws_fg_color_t foreground;
		ws_bg_color_t background;
	} ws_color_t;

	/* Console Color Configuration */
	ws_color_t ws_getCurrentColors();
	ws_color_t ws_getDefaultColors();
	void ws_setDefaultColors(ws_color_t c);
	void ws_setForegroundDefault(ws_fg_color_t c);
	void ws_setBackgroundDefault(ws_bg_color_t c);
	ws_error_t ws_setForegroundColor(ws_fg_color_t color);
	ws_error_t ws_setBackgroundColor(ws_bg_color_t color);
	ws_error_t ws_setConsoleColors(ws_color_t colors);

	/* Stream configurations. */
	typedef enum {
		WS_INPUT,  /* Input stream. Defaults to stdin. */
		WS_OUTPUT, /* Output stream. Defaults to stdout. */
		WS_ERROR_S /* Error stream. Defaults to stderr. */
	} ws_stream;

	// void ws_setStream(ws_stream type, FILE* stream);

	/* Cursors */
	typedef enum {
		WS_CURSOR_LEFT = 0x4b,
		WS_CURSOR_RIGHT = 0x4d,
		WS_CURSOR_UP = 0x48,
		WS_CURSOR_DOWN = 0x50,
	} ws_cursor_t;

	void ws_moveCursor(ws_cursor_t direction);
	void ws_moveCursor_n(ws_cursor_t direction, size_t n);

	/* General operations */
	ws_error_t ws_registerCommand(const ws_command_t c);
	void ws_deregisterCommand(const ws_command_t c);
	ws_error_t ws_executeCommand(char* commandBuf);
	ws_error_t ws_terminalMain();

	/* Console Setup */
	void ws_setAsciiDeleteAsBackspace(bool b);
	void ws_setConsoleLocale();
	void ws_setConsolePrefix(const char* newPrefix);
	void ws_initializeDefaultStreams();

	/* Utility functions */
	bool ws_promptUser(const char* format, ...);
	bool ws_compareCommands(const ws_command_t c1, const ws_command_t c2);
	void ws_cleanAll();

	/* Logger */
#ifndef NO_WS_LOGGING
	typedef enum {
		WS_LOG,
		WS_DEBUG,
		WS_INFO,
		WS_WARN,
		WS_ERROR,
		WS_FATAL
	} ws_logtype_t;

	void ws_logger(ws_logtype_t type, const char* format, ...);
	void ws_vlogger(ws_logtype_t type, const char* format, va_list args);
	void ws_setLoggerColors(ws_logtype_t type, ws_fg_color_t fg, ws_bg_color_t bg);
#endif // NO_WS_LOGGING

	bool ws_internal_startsWith(const char* str, const char* prefix);
	void ws_setEnvironment(char**);

#ifdef __cplusplus
}
#endif
#endif // COMMAND_HANDLER_H