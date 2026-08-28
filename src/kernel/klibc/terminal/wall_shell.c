/**
 * @file wall_shell.c
 * @author MalTheLegend104
 * @brief Main source file for WallShell.
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

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// General header & compiler config
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#include <terminal/wall_shell.h>

/* Disable unused parameter warnings. This only affects this file. */
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Internal Utility Functions
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/**
 * @internal
 * @brief Internal function to concat a single char to the end of a string.
 * If the string is already at max length (including room for '\0') then the string is returned unchanged.
 *
 * @param string String to add the character to.
 * @param c Character to add to the string.
 * @param size Size of the entire buffer.
 * @return char* Same as the string passed through.
 */
char* ws_internal_strcat_c(char* string, char c, size_t size) {
	// Find the current length of the string
	size_t current_length = strlen(string);

	// If adding something would make the string too long, we return unchanged
	if (current_length + 1 >= size) return string;

	// Add the character to the end of the string
	string[current_length] = c;
	string[current_length + 1] = '\0';

	return string;
}

/**
 * @internal
 * @brief Internal function to insert a single char into a string.
 * If the string is already at max length (including room for '\0') then the string is returned unchanged.
 *
 * @param string String to add the character to.
 * @param buf_size Size of the entire buffer.
 * @param c Character to add to the string.
 * @param position Position that the character is inserted into. It should be index + 1
 * @return char* Same as the string passed through.
 */
char* ws_internal_insert_c(char* string, size_t buf_size, char c, size_t position) {
	size_t current_length = strlen(string);
	if (current_length + 1 >= buf_size) return string;

	for (size_t i = current_length; i > position - 1; i--) {
		string[i] = string[i - 1];
	}
	string[position - 1] = c;

	return string;
}

/**
 * @internal
 * @brief Checks if a string starts with another string.
 *
 * As far as I know, there is nothing in libc to do this, although I might've just overlooked something.
 * Regardless, this is a pretty simple function, just a little bit of pointer magic.
 * It's an internal function, but you can easily use it using either extern or adding the declaration to `wallshell_config.h`.
 *
 * @param str String to check
 * @param prefix Thing to check that the other starts with.
 * @return true If the string starts with prefix.
 * @return false If the string does not start with prefix.
 */
bool ws_internal_startsWith(const char* str, const char* prefix) {
	while (*prefix && *str) {
		if (*prefix != *str) {
			return false;
		}
		prefix++;
		str++;
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Streams
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/**
 * @brief Sets the stream to the provided one.
 * @param type Type of stream to change.
 * @param stream Stream you wish to change it to.
 */
// void ws_setStream(ws_stream type, FILE* stream) {

// }

/**
 * @brief Initialize all streams to their defaults. All default to their std-versions. (stdout, stderr, stdin)
 */
void ws_initializeDefaultStreams() {
	// ws_setStream(WS_INPUT, stdin);
	// ws_setStream(WS_ERROR_S, stderr);
	// ws_setStream(WS_OUTPUT, stdout);
}

/**
 * @internal
 * @brief Internal function to reset streams to their default state.
 */
void ws_internal_cleanStreams() {

}

#ifndef CLEAR_ROW
#define CLEAR_ROW printf( "\033[M");
#endif // CLEAR_ROW

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Custom Console Setup
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// For some systems (mostly POSIX), backspace gets sent as ascii delete rather than \b
bool backspace_as_ascii_delete = false;

// To aid portability, we allow the user to set backspace_as_ascii_delete
/**
 * @brief Some consoles send backspace as ASCII delete (0x7f) instead of '\\b'.
 *
 * If your system does this, set this to true. This only needs to be done if CUSTOM_WS_SETUP is defined.
 * For POSIX this is typically true, for Windows this is false.
 *
 * @param b Bool to set backspace_as_ascii_delete to.
 */
void ws_setAsciiDeleteAsBackspace(bool b) { backspace_as_ascii_delete = b; }

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Console Colors
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#ifdef THREADED_SUPPORT
ws_mutex_t* color_mutex = NULL;
void ws_internal_color_mutex_check() {
	if (!color_mutex) color_mutex = ws_createMutex();
	// We don't really care if it's NULL.
}
#define COLOR_MUTEX_CHECK ws_internal_color_mutex_check()
#define LOCK_COLOR_MUTEX ws_lockMutex(color_mutex)
#define UNLOCK_COLOR_MUTEX ws_unlockMutex(color_mutex)

#else
#define COLOR_MUTEX_CHECK
#define LOCK_COLOR_MUTEX
#define UNLOCK_COLOR_MUTEX
#endif
ws_color_t default_colors = { WS_FG_DEFAULT, WS_BG_DEFAULT };
ws_color_t current_colors = { WS_FG_DEFAULT, WS_BG_DEFAULT };

/**
 * @internal
 * @brief Resets all color variables to their default state.
 */
void ws_internal_cleanColors() {
#ifdef THREADED_SUPPORT
	if (color_mutex) ws_destroyMutex(color_mutex);
	color_mutex = NULL;
#endif // THREADED_SUPPORT
	default_colors.foreground = WS_FG_DEFAULT;
	default_colors.background = WS_BG_DEFAULT;
	current_colors.foreground = WS_FG_DEFAULT;
	current_colors.background = WS_BG_DEFAULT;
}

/**
 * @internal
 * @brief Updates the current colors.
 *
 * @return ws_error_t WS_OUT_STREAM_NOT_SET if the output stream hasn't been set yet. WS_NO_ERROR otherwise.
 */
ws_error_t ws_internal_updateColors() {
	COLOR_MUTEX_CHECK;
	LOCK_COLOR_MUTEX;
	// if (!ws_out_stream) return WS_OUT_STREAM_NOT_SET;
	if (current_colors.foreground == WS_FG_DEFAULT) {
		current_colors.foreground = default_colors.foreground;
	}
	if (current_colors.background == WS_BG_DEFAULT) {
		current_colors.background = default_colors.background;
	}
	SET_WS_COLORS(current_colors.foreground, current_colors.background);
	UNLOCK_COLOR_MUTEX;
	return WS_NO_ERROR;
}

/**
 * @brief Set the default foreground color.
 * @param c Color to set the default to.
 */
void ws_setForegroundDefault(ws_fg_color_t c) {
	COLOR_MUTEX_CHECK;
	LOCK_COLOR_MUTEX;
	default_colors.foreground = c;
	UNLOCK_COLOR_MUTEX;
}

/**
 * @brief Set the default background color.
 * @param c Color to set the default to.
 */
void ws_setBackgroundDefault(ws_bg_color_t c) {
	COLOR_MUTEX_CHECK;
	LOCK_COLOR_MUTEX;
	default_colors.background = c;
	UNLOCK_COLOR_MUTEX;
}

/**
 * @brief Set the default colors to the provided ones.
 * @param c Colors to set the defaults to.
 */
void ws_setDefaultColors(ws_color_t c) {
	ws_setForegroundDefault(c.foreground);
	ws_setBackgroundDefault(c.background);
}

/**
 * @brief Get the current console colors.
 * @return  The ws_color_t of the current colors.
 */
ws_color_t ws_getCurrentColors() { return current_colors; }

/**
 * @brief Get the current default colors.
 * @return The ws_color_t of the default colors.
 */
ws_color_t ws_getDefaultColors() { return default_colors; }

/**
 * @brief Set the background and foreground colors to the provided ones.
 * @param colors Colors to set the background and foreground to.
 * @return Can return WS_OUT_STREAM_NOT_SET if the stream hasn't be initialized.
 */
ws_error_t ws_setConsoleColors(ws_color_t colors) {
	COLOR_MUTEX_CHECK;
	LOCK_COLOR_MUTEX;
	current_colors.foreground = colors.foreground;
	current_colors.background = colors.background;
	UNLOCK_COLOR_MUTEX;
	return ws_internal_updateColors();
}

/**
 * @brief Sets the foreground color to the provided color.
 * @param color Color to set the foreground to.
 * @return Can return WS_OUT_STREAM_NOT_SET if the stream hasn't be initialized.
 */
ws_error_t ws_setForegroundColor(ws_fg_color_t color) {
	COLOR_MUTEX_CHECK;
	LOCK_COLOR_MUTEX;
	current_colors.foreground = color;
	UNLOCK_COLOR_MUTEX;
	return ws_internal_updateColors();
}

/**
 * @brief Sets the background color to the provided color.
 * @param color Color to set the background to.
 * @return Can return WS_OUT_STREAM_NOT_SET if the stream hasn't be initialized.
 */
ws_error_t ws_setBackgroundColor(ws_bg_color_t color) {
	COLOR_MUTEX_CHECK;
	LOCK_COLOR_MUTEX;
	current_colors.background = color;
	UNLOCK_COLOR_MUTEX;
	return ws_internal_updateColors();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Threads
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#ifdef THREADED_SUPPORT
#ifndef CUSTOM_THREADS

#ifdef _WIN32
/**
 * @brief Locks the provided mutex.
 * @param mut Mutex to be locked.
 */
void ws_lockMutex(ws_mutex_t* mut) { EnterCriticalSection(mut); }
/**
 * @brief Unlocks the provided mutex.
 * @param mut Mutex to be unlocked.
 */
void ws_unlockMutex(ws_mutex_t* mut) { LeaveCriticalSection(mut); }

/**
 * @brief Create a mutex.
 * @return Pointer to a mutex object if successful, NULL otherwise.
 */
ws_mutex_t* ws_createMutex() {
	ws_mutex_t* mut = (ws_mutex_t*) malloc(sizeof(ws_mutex_t));
	if (mut == NULL) return NULL;
	InitializeCriticalSection(mut);
	return mut;
}

/**
 * @brief Destroys the provided mutex.
 * @param mut mutex to be destroyed.
 */
void ws_destroyMutex(ws_mutex_t* mut) {
	if (!mut) return;
	ws_lockMutex(mut);
	DeleteCriticalSection(mut);
	free(mut);
}

/**
 * @brief Gets the threadID of the calling thread.
 * @return ws_thread_id_t relating to the calling thread.
 */
ws_thread_id_t ws_getThreadID() { return GetCurrentThreadId(); }

/**
 * @internal
 * @brief Prints the threadID related to the calling thread.
 *
 * @param stream Output stream for fprintf.
 */
void ws_internal_printThreadID(FILE* stream) { fprintf(stream, "%lu", ws_getThreadID()); }

/**
 * @brief Sleep function wrapper.
 * @param ms Sleep time in milliseconds.
 */
void ws_sleep(size_t ms) {
	Sleep(ms);
}
#else

/**
 * @brief Locks the provided mutex.
 * @param mut Mutex to be locked.
 */
void ws_lockMutex(ws_mutex_t* mut) { pthread_mutex_lock(mut); }

/**
 * @brief Unlocks the provided mutex.
 * @param mut Mutex to be unlocked.
 */
void ws_unlockMutex(ws_mutex_t* mut) { pthread_mutex_unlock(mut); }

/**
 * @brief Create a mutex.
 * @return Pointer to a mutex object if successful, NULL otherwise.
 */
ws_mutex_t* ws_createMutex() {
	ws_mutex_t* mut = (ws_mutex_t*) malloc(sizeof(ws_mutex_t));
	if (mut == NULL) return NULL;
	pthread_mutex_init(mut, NULL);
	return mut;
}

/**
 * @brief Destroys the provided mutex.
 * @param mut mutex to be destroyed.
 */
void ws_destroyMutex(ws_mutex_t* mut) {
	if (!mut) return;
	// ws_lockMutex(mut); // not supposed to lock it on POSIX
	pthread_mutex_destroy(mut);
	free(mut);
}
/**
 * @brief Gets the threadID of the calling thread.
 * @return ws_thread_id_t relating to the calling thread.
 */
ws_thread_id_t ws_getThreadID() { return pthread_self(); }
/**
 * @internal
 * @brief Prints the threadID related to the calling thread.
 *
 * @param stream Output stream for fprintf.
 */
void ws_internal_printThreadID(FILE* stream) { fprintf(stream, "%zu", ws_getThreadID()); }

/**
 * @brief Sleep function wrapper.
 * @param ms Sleep time in milliseconds.
 */
void ws_sleep(size_t ms) {
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000; // Convert remaining milliseconds to nanoseconds

	nanosleep(&ts, NULL);
}
#endif // _WIN32
#endif // CUSTOM_THREADS

/**
 * @brief Gets the value stored by the atomic bool.
 * @param ab Atomic bool object pointer.
 * @return False if bool does not exist, otherwise the value contained by the bool.
 */
bool ws_getAtomicBool(ws_atomic_bool_t* ab) {
	if (!ab) return false;
	bool ret;
	ws_lockMutex(ab->mut);
	ret = ab->b;
	ws_unlockMutex(ab->mut);
	return ret;
}

/**
 * @brief Sets the value of the provided atomic bool.
 * @param ab Atomic bool object.
 * @param b Value to set the bool to.
 */
void ws_setAtomicBool(ws_atomic_bool_t* ab, bool b) {
	if (!ab) return;
	ws_lockMutex(ab->mut);
	ab->b = b;
	ws_unlockMutex(ab->mut);
}

/**
 * @brief Creates an atomic bool.
 * @param b Initial value held by the bool.
 * @return NULL if it couldn't be created, pointer to the object otherwise.
 */
ws_atomic_bool_t* ws_createAtomicBool(bool b) {
	ws_atomic_bool_t* ab = (ws_atomic_bool_t*) malloc(sizeof(ws_atomic_bool_t));
	if (ab == NULL) return NULL;
	ab->b = b;
	ab->mut = ws_createMutex();
	return ab;
}

/**
 * @brief Destroys the provided atomic bool.
 * @param ab Atomic bool to destroy.
 */
void ws_destroyAtomicBool(ws_atomic_bool_t* ab) {
	if (!ab) return;
	ws_lockMutex(ab->mut);
	ws_destroyMutex(ab->mut);
	free(ab);
}

#endif // THREADED_SUPPORT

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Logging Functions
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#ifndef NO_WS_LOGGING
#ifdef THREADED_SUPPORT
ws_mutex_t* logging_mutex = NULL;
#define LOCK_LOGGING_MUTEX ws_lockMutex(logging_mutex)
#define UNLOCK_LOGGING_MUTEX ws_unlockMutex(logging_mutex)

typedef struct {
	char* name;
	ws_thread_id_t id;
} ws_thread_map_t;

ws_thread_map_t* thread_map = NULL;
size_t thread_map_size = 0;
size_t thread_map_current = 0;

ws_mutex_t* thread_map_mut = NULL;
/**
 * @brief Sets the name of the calling thread. This will only be printed if threadID is true.
 * @param name Name of the thread.
 */
void ws_setThreadName(char* name) {
	if (!thread_map_mut) {
		thread_map_mut = ws_createMutex();
		if (!thread_map_mut) return;
	}
	ws_lockMutex(thread_map_mut);
	if (!thread_map) {
		thread_map = (ws_thread_map_t*) calloc(1, sizeof(ws_thread_map_t));
		if (!thread_map) { ws_unlockMutex(thread_map_mut); return; }
		thread_map_size++;
	}
	if (thread_map_size - 1 < thread_map_current) {
		ws_thread_map_t* temp = realloc(thread_map, (thread_map_size + 1) * sizeof(ws_thread_map_t));
		if (!temp) { ws_unlockMutex(thread_map_mut); return; }
		thread_map = temp;
		thread_map_size++;
	}
	char* thread_name = calloc(strlen(name), sizeof(char));
	if (!thread_name) { ws_unlockMutex(thread_map_mut); return; }
	strcpy(thread_name, name);

	thread_map[thread_map_current].name = thread_name;
	thread_map[thread_map_current].id = ws_getThreadID();

	thread_map_current++;
	ws_unlockMutex(thread_map_mut);
}

/**
 * @brief Removes the name of the provided thread.
 * @param name Name of the thread.
 */
void ws_removeThreadName(const char* name) {
	if (!thread_map_mut) {
		thread_map_mut = ws_createMutex();
		if (!thread_map_mut) return;
	}
	ws_lockMutex(thread_map_mut);

	if (!thread_map) { ws_unlockMutex(thread_map_mut); return; }
	if (thread_map_current == 0) { ws_unlockMutex(thread_map_mut); return; }
	if (thread_map_size == 0) { ws_unlockMutex(thread_map_mut); return; } // This should be impossible.

	for (int i = 0; i < thread_map_current; i++) {
		if (strcmp(thread_map[thread_map_current].name, name) == 0) {
			free(thread_map[thread_map_current].name);
			for (int j = i; j < thread_map_current - 1; j++) {
				thread_map[j].id = thread_map[j + 1].id;
				thread_map[j].name = thread_map[j + 1].name;
			}
			ws_unlockMutex(thread_map_mut);
			return;
		}
	}

	ws_unlockMutex(thread_map_mut);
}

/**
 * @brief Prints the threadID of the calling thread.
 */
void ws_printThreadID() {
	if (!thread_map_mut) {
		thread_map_mut = ws_createMutex();
		if (!thread_map_mut) return;
	}
	ws_lockMutex(thread_map_mut);
	ws_thread_id_t cur = ws_getThreadID();
	for (int i = 0; i < thread_map_current; i++) {
		if (thread_map[i].id == cur) {
			printf("%s", thread_map[i].name);
			goto exit;
		}
	}
	ws_internal_printThreadID(ws_out_stream);
exit:
	ws_unlockMutex(thread_map_mut);
}

bool printThreadID = true;
/**
 * @brief Set print threadID, which prints the threadID of function calling `ws_logger`. Defaults to on.
 * @param b True to turn on, false to turn off.
 */
void ws_doPrintThreadID(bool b) { printThreadID = b; }
#else
#define LOCK_LOGGING_MUTEX
#define UNLOCK_LOGGING_MUTEX
#endif

/**
 * @internal
 * @brief Checks the logging mutex and makes sure the output stream is set.
 */
void ws_internal_logging_check() {
#ifdef THREADED_SUPPORT
	if (!logging_mutex) logging_mutex = ws_createMutex(); // We don't really care if it's NULL.
#endif // THREADED_SUPPORT
	// Make sure we have an out stream.
	// if (!ws_out_stream) ws_setStream(WS_OUTPUT, stdout);
}

#define LOGGING_CHECK ws_internal_logging_check()

ws_color_t log_colors = { WS_FG_WHITE, WS_BG_DEFAULT };
ws_color_t debug_colors = { WS_FG_BRIGHT_GREEN, WS_BG_DEFAULT };
ws_color_t info_colors = { WS_FG_BRIGHT_CYAN, WS_BG_DEFAULT };
ws_color_t warn_colors = { WS_FG_BRIGHT_YELLOW, WS_BG_DEFAULT };
ws_color_t error_colors = { WS_FG_BRIGHT_RED, WS_BG_DEFAULT };
ws_color_t fatal_colors = { WS_FG_RED, WS_BG_DEFAULT };

/**
 * @internal
 * @brief Logger [LOG] function
 *
 * @param format printf format string
 * @param args vprintf va_list
 */
void ws_vlogf(const char* format, va_list args) {
	LOGGING_CHECK;
	LOCK_LOGGING_MUTEX;
	ws_color_t current = ws_getCurrentColors();
	ws_setConsoleColors(log_colors);
	printf("[LOG]  ");

#ifdef THREADED_SUPPORT
	if (printThreadID) {
		printf("[");
		ws_printThreadID();
		printf("]");
	}
#endif
	printf(" ");
	vprintf(format, args);
	printf("\n");
	ws_setConsoleColors(current);
	UNLOCK_LOGGING_MUTEX;
}

/**
 * @internal
 * @brief Logger [DEBUG] function
 *
 * @param format printf format string
 * @param args vprintf va_list
 */
void ws_vdebugf(const char* format, va_list args) {
	LOGGING_CHECK;
	LOCK_LOGGING_MUTEX;
	ws_color_t current = ws_getCurrentColors();
	ws_setConsoleColors(debug_colors);
	printf("[DEBUG]");

#ifdef THREADED_SUPPORT
	if (printThreadID) {
		printf("[");
		ws_printThreadID();
		printf("]");
	}
#endif
	printf(" ");
	vprintf(format, args);
	printf("\n");
	ws_setConsoleColors(current);
	UNLOCK_LOGGING_MUTEX;
}

/**
 * @internal
 * @brief Logger [INFO] function
 *
 * @param format printf format string
 * @param args vprintf va_list
 */
void ws_vinfof(const char* format, va_list args) {
	LOGGING_CHECK;
	LOCK_LOGGING_MUTEX;
	ws_color_t current = ws_getCurrentColors();
	ws_setConsoleColors(info_colors);
	printf("[INFO] ");

#ifdef THREADED_SUPPORT
	if (printThreadID) {
		printf("[");
		ws_printThreadID();
		printf("]");
	}
#endif
	printf(" ");
	vprintf(format, args);
	printf("\n");

	ws_setConsoleColors(current);
	UNLOCK_LOGGING_MUTEX;
}

/**
 * @internal
 * @brief Logger [WARN] function
 *
 * @param format printf format string
 * @param args vprintf va_list
 */
void ws_vwarnf(const char* format, va_list args) {
	LOGGING_CHECK;
	LOCK_LOGGING_MUTEX;
	ws_color_t current = ws_getCurrentColors();
	ws_setConsoleColors(warn_colors);
	printf("[WARN] ");

#ifdef THREADED_SUPPORT
	if (printThreadID) {
		printf("[");
		ws_printThreadID();
		printf("]");
	}
#endif
	printf(" ");
	vprintf(format, args);
	printf("\n");

	ws_setConsoleColors(current);
	UNLOCK_LOGGING_MUTEX;
}

/**
 * @internal
 * @brief Logger [ERROR] function
 *
 * @param format printf format string
 * @param args vprintf va_list
 */
void ws_verrorf(const char* format, va_list args) {
	LOGGING_CHECK;
	LOCK_LOGGING_MUTEX;
	ws_color_t current = ws_getCurrentColors();
	ws_setConsoleColors(error_colors);
	printf("[ERROR]");

#ifdef THREADED_SUPPORT
	if (printThreadID) {
		printf("[");
		ws_printThreadID();
		printf("]");
	}
#endif
	printf(" ");
	vprintf(format, args);
	printf("\n");

	ws_setConsoleColors(current);
	UNLOCK_LOGGING_MUTEX;
}

/**
 * @internal
 * @brief Logger [FATAL] function
 *
 * @param format printf format string
 * @param args vprintf va_list
 */
void ws_vfatalf(const char* format, va_list args) {
	LOGGING_CHECK;
	LOCK_LOGGING_MUTEX;
	ws_color_t current = ws_getCurrentColors();
	ws_setConsoleColors(fatal_colors);
	printf("[FATAL]");

#ifdef THREADED_SUPPORT
	if (printThreadID) {
		printf("[");
		ws_printThreadID();
		printf("]");
	}
#endif
	printf(" ");
	vprintf(format, args);
	printf("\n");

	ws_setConsoleColors(current);
	UNLOCK_LOGGING_MUTEX;
}

/**
 * @brief Logger function for WallShell. vprintf like formatting, automatically adds a newline.
 * @param type Type of logging.
 * @param format printf style formatting string.
 * @param args va_list of arguments.
 */
void ws_vlogger(ws_logtype_t type, const char* format, va_list args) {
	switch (type) {
		case WS_LOG: {
				ws_vlogf(format, args);
				break;
			}
		case WS_DEBUG: {
				ws_vdebugf(format, args);
				break;
			}
		case WS_INFO: {
				ws_vinfof(format, args);
				break;
			}
		case WS_WARN: {
				ws_vwarnf(format, args);
				break;
			}
		case WS_ERROR: {
				ws_verrorf(format, args);
				break;
			}
		case WS_FATAL: {
				ws_vfatalf(format, args);
				break;
			}
		default: {
				vprintf(format, args);
			}
	}
}

/**
 * @brief Logger function for WallShell. Printf like formatting, automatically adds a newline.
 * @param type Type of logging.
 * @param format Printf style formatting string.
 * @param ... Printf style formatting arguments.
 */
void ws_logger(ws_logtype_t type, const char* format, ...) {
	va_list args;
	va_start(args, format);
	ws_vlogger(type, format, args);
	va_end(args);
}

/**
 * @brief Set the logger colors for the specified log type.
 * @param type Type of logging.
 * @param fg Foreground color.
 * @param bg Background color.
 */
void ws_setLoggerColors(ws_logtype_t type, ws_fg_color_t fg, ws_bg_color_t bg) {
	switch (type) {
		case WS_LOG: {
				log_colors.foreground = fg;
				log_colors.background = bg;
				break;
			}
		case WS_INFO: {
				warn_colors.foreground = fg;
				warn_colors.background = bg;
				break;
			}
		case WS_DEBUG: {
				debug_colors.foreground = fg;
				debug_colors.background = bg;
				break;
			}
		case WS_WARN: {
				warn_colors.foreground = fg;
				warn_colors.background = bg;
				break;
			}
		case WS_ERROR: {
				error_colors.foreground = fg;
				error_colors.background = bg;
				break;
			}
		case WS_FATAL: {
				fatal_colors.foreground = fg;
				fatal_colors.background = bg;
				break;
			}
		default: break;
	}
}

/**
 * @internal
 * @brief Resets all logger variables. Resets color, mutex, etc.
 */
void ws_internal_cleanLogger() {
#ifdef THREADED_SUPPORT
	if (logging_mutex) ws_destroyMutex(logging_mutex);
	if (thread_map_mut) ws_destroyMutex(thread_map_mut);
	logging_mutex = NULL;
	printThreadID = true;
	if (thread_map) {
		for (int i = 0; i < thread_map_current; i++) {
			free(thread_map[i].name);
		}
		free(thread_map);
	}
	thread_map_size = 0;
	thread_map_current = 0;
	thread_map = NULL;
#endif
	log_colors = (ws_color_t){ WS_FG_WHITE, WS_BG_DEFAULT };
	debug_colors = (ws_color_t){ WS_FG_BRIGHT_GREEN, WS_BG_DEFAULT };
	info_colors = (ws_color_t){ WS_FG_BRIGHT_CYAN, WS_BG_DEFAULT };
	warn_colors = (ws_color_t){ WS_FG_BRIGHT_YELLOW, WS_BG_DEFAULT };
	error_colors = (ws_color_t){ WS_FG_BRIGHT_RED, WS_BG_DEFAULT };
	fatal_colors = (ws_color_t){ WS_FG_RED, WS_BG_DEFAULT };
}

#endif // NO_WS_LOGGING

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Register Command & Internal Commands
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

#ifdef DISABLE_MALLOC
ws_command_t commands[COMMAND_LIMIT];
size_t command_size = COMMAND_LIMIT;
#else
ws_command_t* commands;
size_t command_size = 0;
#endif

size_t current_command_spot = 0;

char previousCommands[PREVIOUS_BUF_SIZE][MAX_COMMAND_BUF];
size_t previous_commands_size;

/**
 * @internal
 * @brief Resets all command variables. Resets command list, previousCommands, etc.
 */
void ws_internal_cleanCommands() {
#ifndef DISABLE_MALLOC
	free(commands);
	commands = NULL;
	command_size = 0;
#else
	for (int i = 0; i < current_command_spot; i++) {
		commands[current_command_spot] = (ws_command_t){ 0 };
	}
	command_size = COMMAND_LIMIT;
#endif // DISABLE_MALLOC
	current_command_spot = 0;

	for (int i = 0; i < PREVIOUS_BUF_SIZE; i++) {
		memset(previousCommands, 0, MAX_COMMAND_BUF);
	}
	previous_commands_size = 0;
}

/**
 * @brief Register the command to the command handler.
 * @param c Command to be registered.
 * @return Can return WS_COMMAND_LIMIT_REACHED if DISABLE_MALLOC is defined, and WS_OUT_OF_MEMORY if not.
 */
ws_error_t ws_registerCommand(const ws_command_t c) {
#ifdef DISABLE_MALLOC
	if (current_command_spot != COMMAND_LIMIT) {
		commands[current_command_spot] = c;
		current_command_spot++;
	} else {
		return WS_COMMAND_LIMIT_REACHED;
	}
#else
	if (!commands) {
		commands = malloc(sizeof(ws_command_t));
		command_size = 1;
	} else if (current_command_spot >= command_size) {
		bool was_one = false;
		if (command_size == 1) {
			was_one = true;
			command_size++;
		}
		// realloc invalidates the old pointer on call, but leaves it alone if it cant find the memory.
		// If this function returns with an out of memory error, the shell is still usable.
		ws_command_t* new_ptr = realloc(commands, (size_t) ((double) command_size * sizeof(ws_command_t) * 1.5));
		if (!new_ptr) {
			if (was_one)
				command_size--;
			return WS_OUT_OF_MEMORY;
		} else {
			commands = new_ptr;
		}
		command_size = (size_t) ((double) command_size * 1.5);
	}
	//memcpy(commands[current_command_spot], &c, sizeof(command_t));
	commands[current_command_spot] = c;
	current_command_spot++;
#endif
	return WS_NO_ERROR;
}

/**
 * @brief Deregister the provided command.
 * @param c Command to be deregistered. If it doesn't exist (not already registered), nothing happens.
 */
void ws_deregisterCommand(const ws_command_t c) {
	for (int i = 0; i < current_command_spot; i++) {
		if (ws_compareCommands(commands[i], c)) {
			for (int j = i; j < current_command_spot - 1; j++) {
				// Nothing is allocated through malloc. If something is, it's on the user to free it either before/after calling this.
				commands[j] = commands[j + 1];
			}
			commands[current_command_spot - 1] = (ws_command_t){ 0 };
			current_command_spot--;
			return;
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Argument Parsing
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/**
 * This struct is for internal use only.
 * The end user will only see an opaque pointer to this.
 * This is only for use with argparse-adjacent functions:
 * - Returned by ws_parse_args()
 * - ws_get<type>(char* argument_name)
 */
typedef struct wallshell_internal_argparse_t {
	const ws_command_argument_t* argument;
	union {
		bool flag;
		bool boolean;

		char c;
		const char* str;

		int8_t int8;
		int16_t int16;
		int32_t int32;
		int64_t int64;

		uint8_t uint8;
		uint16_t uint16;
		uint32_t uint32;
		uint64_t uint64;

		float f;
		double d;
	};
	struct wallshell_internal_argparse_t* next;
} wallshell_argparse_t;

/**
 * @internal
 * @brief Definition of the opaque ws_context_t type declared in wall_shell.h.
 *
 * Holds the parsed argument values for whichever command WallShell is currently dispatching.
 * There is a single one of these, reused for every command invocation (see ws_getCurrentContext()).
 */
struct ws_context {
	const ws_command_t* command; // Command this context is currently associated with.
	bool parsed;                 // Whether ws_parse_args() has already been run for this invocation.
#ifdef DISABLE_MALLOC
	wallshell_argparse_t nodes[MAX_ARGPARSE_NODES];
	size_t node_count;
#else
	wallshell_argparse_t* head;
#endif // DISABLE_MALLOC
};

static ws_context_t ws_internal_current_context = { 0 };

/* WallShell should technically only be run from one thread at a time.
 * In threaded mode, it should be given it's own thread.
 * There is a chance that the command gets dispatched to it's own different thread
 * This is for protection against that so we don't end up in a bad state.
 */
#ifdef THREADED_SUPPORT
ws_mutex_t* context_mutex = NULL;
void ws_internal_context_mutex_check() {
	if (!context_mutex) context_mutex = ws_createMutex();
}
#define CONTEXT_MUTEX_CHECK ws_internal_context_mutex_check()
#define LOCK_CONTEXT_MUTEX ws_lockMutex(context_mutex)
#define UNLOCK_CONTEXT_MUTEX ws_unlockMutex(context_mutex)
#else
#define CONTEXT_MUTEX_CHECK
#define LOCK_CONTEXT_MUTEX
#define UNLOCK_CONTEXT_MUTEX
#endif // THREADED_SUPPORT

/**
 * @internal
 * @brief Frees/resets whatever is currently stored in the context and re-associates it with a (potentially new) command.
 */
void ws_internal_context_reset(ws_context_t* ctx, const ws_command_t* command) {
#ifndef DISABLE_MALLOC
	wallshell_argparse_t* current = ctx->head;
	while (current) {
		wallshell_argparse_t* next = current->next;
		free(current);
		current = next;
	}
	ctx->head = NULL;
#else
	ctx->node_count = 0;
#endif // DISABLE_MALLOC
	ctx->command = command;
	ctx->parsed = false;
}

/**
 * @internal
 * @brief Resets the global argparse context. Called as part of ws_cleanAll().
 */
void ws_internal_cleanContext() {
	ws_internal_context_reset(&ws_internal_current_context, NULL);
#ifdef THREADED_SUPPORT
	if (context_mutex) ws_destroyMutex(context_mutex);
	context_mutex = NULL;
#endif // THREADED_SUPPORT
}

/**
 * @brief Returns the argparse context for the command currently being executed by WallShell.
 * @return ws_context_t* Context for the currently executing command, or NULL if no command is currently being dispatched.
 */
ws_context_t* ws_getCurrentContext() {
	if (!ws_internal_current_context.command) return NULL;
	return &ws_internal_current_context;
}

/**
 * @internal
 * @brief Strips up to two leading dashes off of a name, for comparison purposes.
 */
const char* ws_internal_stripDashes(const char* name) {
	if (!name) return name;
	if (name[0] == '-') {
		name++;
		if (name[0] == '-') name++;
	}
	return name;
}

/**
 * @internal
 * @brief Checks if a token matches an argument definition's name or shortform, dash-insensitively.
 */
bool ws_internal_argNameMatches(const ws_command_argument_t* arg, const char* token) {
	const char* stripped_token = ws_internal_stripDashes(token);
	if (arg->name && strcmp(ws_internal_stripDashes(arg->name), stripped_token) == 0) return true;
	if (arg->shortform && strcmp(ws_internal_stripDashes(arg->shortform), stripped_token) == 0) return true;
	return false;
}

/**
 * @internal
 * @brief Finds a non-GENERIC argument definition on a command matching the given (dashed) token.
 */
const ws_command_argument_t* ws_internal_findNamedArg(const ws_command_t* cmd, const char* token) {
	for (size_t i = 0; i < cmd->arguments_count; i++) {
		if (cmd->arguments[i].type == WS_ARG_TYPE_GENERIC) continue;
		if (ws_internal_argNameMatches(&cmd->arguments[i], token)) return &cmd->arguments[i];
	}
	return NULL;
}

/**
 * @internal
 * @brief Finds the (index'th) GENERIC argument definition of a command, used to match positional arguments in order.
 */
const ws_command_argument_t* ws_internal_findGenericArg(const ws_command_t* cmd, size_t index) {
	size_t seen = 0;
	for (size_t i = 0; i < cmd->arguments_count; i++) {
		if (cmd->arguments[i].type != WS_ARG_TYPE_GENERIC) continue;
		if (seen == index) return &cmd->arguments[i];
		seen++;
	}
	return NULL;
}

/**
 * @internal
 * @brief Looks up a previously-parsed node for the given argument name, dash-insensitively.
 * NULL if not found.
 */
const wallshell_argparse_t* ws_internal_findNode(const ws_context_t* ctx, const char* name) {
	if (!ctx || !name) return NULL;
	const char* stripped = ws_internal_stripDashes(name);
#ifdef DISABLE_MALLOC
	for (size_t i = 0; i < ctx->node_count; i++) {
		const wallshell_argparse_t* node = &ctx->nodes[i];
		if (node->argument && ws_internal_argNameMatches(node->argument, stripped)) return node;
	}
#else
	for (const wallshell_argparse_t* node = ctx->head; node; node = node->next) {
		if (node->argument && ws_internal_argNameMatches(node->argument, stripped)) return node;
	}
#endif // DISABLE_MALLOC
	return NULL;
}

/**
 * @internal
 * @brief Allocates (or claims a static slot for) a new node in the context, returning it to be filled in by the caller.
 * @return Pointer to the new node, or NULL if allocation failed / the static array is full.
 */
wallshell_argparse_t* ws_internal_newNode(ws_context_t* ctx) {
#ifdef DISABLE_MALLOC
	if (ctx->node_count >= MAX_ARGPARSE_NODES) return NULL;
	wallshell_argparse_t* node = &ctx->nodes[ctx->node_count++];
	node->next = NULL;
	return node;
#else
	wallshell_argparse_t* node = (wallshell_argparse_t*) malloc(sizeof(wallshell_argparse_t));
	if (!node) return NULL;
	node->next = ctx->head;
	ctx->head = node;
	return node;
#endif // DISABLE_MALLOC
}

/**
 * @internal
 * @brief Prints a formatted argument parsing error to WallShell's error stream.
 */
void ws_internal_argError(const char* format, ...) {
	ws_setConsoleColors((ws_color_t) { WS_FG_BRIGHT_RED, WS_BG_DEFAULT });
	va_list args;
	va_start(args, format);
	// vprintf( format, args);
	vprintf(format, args);
	va_end(args);
	// printf( "\n");
	printf("\n");
	ws_setConsoleColors(ws_getDefaultColors());
}

/**
 * @internal
 * @brief Parses a raw string into the correct union member for an argument's type, storing it in a new context node.
 * On failure, prints to the output stream
 *
 * @return true on success, false if the value couldn't be parsed as the argument's declared type.
 */
bool ws_internal_parseAndStore(ws_context_t* ctx, const ws_command_argument_t* arg, const char* value) {
	wallshell_argparse_t* node = ws_internal_newNode(ctx);
	if (!node) {
		ws_internal_argError("Too many arguments provided.");
		return false;
	}
	node->argument = arg;

	char* end = NULL;
	switch (arg->type) {
		case WS_ARG_TYPE_GENERIC:
		case WS_ARG_TYPE_STRING: node->str = value; break;
		case WS_ARG_TYPE_CHAR: node->c = value[0]; break;
		case WS_ARG_TYPE_INT8: node->int8 = (int8_t) strtol(value, &end, 0); break;
		case WS_ARG_TYPE_INT16: node->int16 = (int16_t) strtol(value, &end, 0); break;
		case WS_ARG_TYPE_INT32: node->int32 = (int32_t) strtol(value, &end, 0); break;
		case WS_ARG_TYPE_INT64: node->int64 = (int64_t) strtoll(value, &end, 0); break;
		case WS_ARG_TYPE_UINT8: node->uint8 = (uint8_t) strtoul(value, &end, 0); break;
		case WS_ARG_TYPE_UINT16: node->uint16 = (uint16_t) strtoul(value, &end, 0); break;
		case WS_ARG_TYPE_UINT32: node->uint32 = (uint32_t) strtoul(value, &end, 0); break;
		case WS_ARG_TYPE_UINT64: node->uint64 = (uint64_t) strtoull(value, &end, 0); break;
		case WS_ARG_TYPE_FLOAT: node->f = strtof(value, &end); break;
		case WS_ARG_TYPE_DOUBLE: node->d = strtod(value, &end); break;
		default: end = (char*) value;  break; // FLAG/BOOL never hit this path.
	}

	if ((arg->type != WS_ARG_TYPE_STRING && arg->type != WS_ARG_TYPE_GENERIC && arg->type != WS_ARG_TYPE_CHAR) && (end == value)) {
		ws_internal_argError("Argument \"%s\" expects a numeric value, got \"%s\".", arg->name, value);
#ifdef DISABLE_MALLOC
		ctx->node_count--;
#else
		ctx->head = node->next;
		free(node);
#endif // DISABLE_MALLOC
		return false;
	}
	return true;
}

/**
 * @internal
 * @brief Checks if a token looks like a valid t/f/true/false value for a BOOL argument.
 */
bool ws_internal_isBoolToken(const char* token) {
	if (!token) return false;
	return strcmp(token, "t") == 0 || strcmp(token, "f") == 0 || strcmp(token, "true") == 0 || strcmp(token, "false") == 0;
}

bool ws_internal_parseBoolToken(const char* token) {
	return token[0] == 't' || token[0] == 'T';
}

/**
 * @brief Parse and validate the arguments for the current command.
 * @see wall_shell.h for full documentation.
 */
bool ws_parse_args(ws_context_t* ctx, int argc, char** argv) {
	if (!ctx || !ctx->command) return false;
	const ws_command_t* cmd = ctx->command;
	ws_internal_context_reset(ctx, cmd);

	size_t generic_seen = 0;
	for (int i = 1; i < argc; i++) {
		char* token = argv[i];
		if (token[0] == '-' && token[1] != '\0') {
			const ws_command_argument_t* arg = ws_internal_findNamedArg(cmd, token);
			if (!arg) {
				ws_internal_argError("Unknown argument: %s", token);
				return false;
			}

			if (arg->type == WS_ARG_TYPE_FLAG) {
				wallshell_argparse_t* node = ws_internal_newNode(ctx);
				if (!node) {
					ws_internal_argError("Too many arguments provided.");
					return false;
				}
				node->argument = arg;
				node->flag = true;
			} else if (arg->type == WS_ARG_TYPE_BOOL) {
				wallshell_argparse_t* node = ws_internal_newNode(ctx);
				if (!node) {
					ws_internal_argError("Too many arguments provided.");
					return false;
				}
				node->argument = arg;
				node->boolean = true;
				if (i + 1 < argc && ws_internal_isBoolToken(argv[i + 1])) {
					node->boolean = ws_internal_parseBoolToken(argv[i + 1]);
					i++;
				}
			} else {
				if (i + 1 >= argc) {
					ws_internal_argError("Argument \"%s\" expects a value.", arg->name);
					return false;
				}
				i++;
				if (!ws_internal_parseAndStore(ctx, arg, argv[i])) return false;
			}
		} else {
			const ws_command_argument_t* generic = ws_internal_findGenericArg(cmd, generic_seen);
			if (!generic) {
				ws_internal_argError("Unexpected argument: %s", token);
				return false;
			}
			generic_seen++;
			if (!ws_internal_parseAndStore(ctx, generic, token)) return false;
		}
	}

	// Validate that all required arguments were provided.
	for (size_t i = 0; i < cmd->arguments_count; i++) {
		if (!cmd->arguments[i].required) continue;
		if (!ws_internal_findNode(ctx, cmd->arguments[i].name)) {
			ws_internal_argError("Missing required argument: %s", cmd->arguments[i].name);
			return false;
		}
	}

	ctx->parsed = true;
	return true;
}

bool ws_has_arg(const ws_context_t* ctx, const char* name) {
	return ws_internal_findNode(ctx, name) != NULL;
}

bool ws_get_flag(const ws_context_t* ctx, const char* name) {
	const wallshell_argparse_t* node = ws_internal_findNode(ctx, name);
	return node ? node->flag : false;
}

bool ws_get_bool(const ws_context_t* ctx, const char* name) {
	const wallshell_argparse_t* node = ws_internal_findNode(ctx, name);
	return node ? node->boolean : false;
}

char ws_get_char(const ws_context_t* ctx, const char* name) {
	const wallshell_argparse_t* node = ws_internal_findNode(ctx, name);
	return node ? node->c : '\0';
}

const char* ws_get_string(const ws_context_t* ctx, const char* name) {
	const wallshell_argparse_t* node = ws_internal_findNode(ctx, name);
	return node ? node->str : NULL;
}

const char* ws_get_generic(const ws_context_t* ctx, const char* name) {
	return ws_get_string(ctx, name);
}

/**
 * @internal
 * @brief Widens any of the signed integer union members into an int64_t based on the argument's declared type.
 */
int64_t ws_internal_widenSigned(const wallshell_argparse_t* node) {
	switch (node->argument->type) {
		case WS_ARG_TYPE_INT8: return node->int8;
		case WS_ARG_TYPE_INT16: return node->int16;
		case WS_ARG_TYPE_INT32: return node->int32;
		case WS_ARG_TYPE_INT64: return node->int64;
		default: return 0;
	}
}

/**
 * @internal
 * @brief Widens any of the unsigned integer union members into a uint64_t based on the argument's declared type.
 */
uint64_t ws_internal_widenUnsigned(const wallshell_argparse_t* node) {
	switch (node->argument->type) {
		case WS_ARG_TYPE_UINT8: return node->uint8;
		case WS_ARG_TYPE_UINT16: return node->uint16;
		case WS_ARG_TYPE_UINT32: return node->uint32;
		case WS_ARG_TYPE_UINT64: return node->uint64;
		default: return 0;
	}
}

int64_t ws_get_int64(const ws_context_t* ctx, const char* name) {
	const wallshell_argparse_t* node = ws_internal_findNode(ctx, name);
	if (!node) return 0;
	return ws_internal_widenSigned(node);
}

uint64_t ws_get_uint64(const ws_context_t* ctx, const char* name) {
	const wallshell_argparse_t* node = ws_internal_findNode(ctx, name);
	if (!node) return 0;
	return ws_internal_widenUnsigned(node);
}

double ws_get_double(const ws_context_t* ctx, const char* name) {
	const wallshell_argparse_t* node = ws_internal_findNode(ctx, name);
	if (!node) return 0.0;
	if (node->argument->type == WS_ARG_TYPE_FLOAT) return (double) node->f;
	return node->d;
}

/**
 * @brief Prints a consistent, auto-generated help entry for a command based on its registered ws_command_argument_t list.
 * @see wall_shell.h for full documentation.
 */
void ws_printCommandHelp(const ws_command_t* command) {
	if (!command) return;

	ws_setConsoleColors((ws_color_t) { WS_FG_RED, WS_BG_DEFAULT });
	printf("\n%s", command->command_name);
	if (command->major != 0 || command->minor != 0 || command->patch != 0) {
		printf(" (v%u.%u.%u)", command->major, command->minor, command->patch);
	}
	printf("\n");

	// Usage line.
	ws_setConsoleColors((ws_color_t) { WS_FG_CYAN, WS_BG_DEFAULT });
	printf("Usage: %s", command->command_name);
	for (size_t i = 0; i < command->arguments_count; i++) {
		const ws_command_argument_t* arg = &command->arguments[i];
		if (arg->type == WS_ARG_TYPE_GENERIC) {
			printf(arg->required ? " <%s>" : " [%s]", arg->name);
		} else {
			printf(arg->required ? " --%s" : " [--%s]", ws_internal_stripDashes(arg->name));
		}
	}
	printf("\n");

	if (command->alias_count > 0 && command->aliases) {
		ws_setConsoleColors((ws_color_t) { WS_FG_YELLOW, WS_BG_DEFAULT });
		printf("\nAliases:\n");
		ws_setConsoleColors((ws_color_t) { WS_FG_GREEN, WS_BG_DEFAULT });
		for (uint8_t i = 0; i < command->alias_count; i++) {
			if (command->aliases[i]) printf("  %s\n", command->aliases[i]);
		}
	}

	bool printed_required_header = false, printed_optional_header = false;
	for (size_t i = 0; i < command->arguments_count; i++) {
		const ws_command_argument_t* arg = &command->arguments[i];
		if (!arg->required) continue;
		if (!printed_required_header) {
			ws_setConsoleColors((ws_color_t) { WS_FG_YELLOW, WS_BG_DEFAULT });
			printf("\nRequired:\n");
			printed_required_header = true;
		}
		ws_setConsoleColors((ws_color_t) { WS_FG_GREEN, WS_BG_DEFAULT });
		if (arg->type == WS_ARG_TYPE_GENERIC) {
			printf("  %-16s", arg->name);
		} else if (arg->shortform) {
			printf("  --%s, -%s", ws_internal_stripDashes(arg->name), ws_internal_stripDashes(arg->shortform));
		} else {
			printf("  --%s", ws_internal_stripDashes(arg->name));
		}
		if (arg->description) printf("  %s", arg->description);
		printf("\n");
	}

	for (size_t i = 0; i < command->arguments_count; i++) {
		const ws_command_argument_t* arg = &command->arguments[i];
		if (arg->required) continue;
		if (!printed_optional_header) {
			ws_setConsoleColors((ws_color_t) { WS_FG_YELLOW, WS_BG_DEFAULT });
			printf("\nOptional:\n");
			printed_optional_header = true;
		}
		ws_setConsoleColors((ws_color_t) { WS_FG_GREEN, WS_BG_DEFAULT });
		if (arg->type == WS_ARG_TYPE_GENERIC) {
			printf("  %-16s", arg->name);
		} else if (arg->shortform) {
			printf("  --%s, -%s", ws_internal_stripDashes(arg->name), ws_internal_stripDashes(arg->shortform));
		} else {
			printf("  --%s", ws_internal_stripDashes(arg->name));
		}
		if (arg->description) printf("  %s", arg->description);
		printf("\n");
	}

	ws_setConsoleColors(ws_getDefaultColors());
	printf("\n");
}

/**
 * @internal
 * @brief Runs the help_func for a command if it has one, otherwise falls back to ws_printCommandHelp().
 */
void ws_internal_showCommandHelp(const ws_command_t* command, int argc, char** argv) {
	if (command->help_func) {
		command->help_func(argc, argv);
	} else {
		ws_printCommandHelp(command);
	}
}

/**
 * @internal
 * @brief Checks argv (skipping argv[0], the command name) for a bare "--help"/"-h" token.
 */
bool ws_internal_wantsHelp(int argc, char** argv) {
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Basic Built-In Commands
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/* Internal clear command */
// const char* clear_aliases[] = { "clr", "cls" };

/**
 * @internal
 * @brief Clear function main command
 */
int clearMain(void) {
#ifdef _WIN32
	// Windows being windows, some escape characters don't work normally in like 2/3 of the terminals
	// This is especially evident in things spawned by AllocConsole()
	system("cls");
#else
	// Unix is much nicer
	printf("\033c");
#endif
	// Sometimes clearing the screen results in the colors getting reset.
	ws_internal_updateColors();
	return 0;
}

/* Internal help command */
const ws_command_argument_t help_args[] = {
	{ WS_ARG_TYPE_GENERIC, false, "command", NULL, "Command to show detailed help for." },
	{ WS_ARG_TYPE_STRING, false, "--search", "-s", "Lists all commands and aliases that start with <string>." },
};

/**
 * @internal
 * @brief Help function search command
 */
void helpSearch(const char* str) {
	ws_setConsoleColors((ws_color_t) { WS_FG_YELLOW, WS_BG_DEFAULT });
	printf("List of commands starting with \"%s\": (A) indicates an alias.\n", str);
	ws_setConsoleColors(ws_getDefaultColors());
	for (int i = 0; i < current_command_spot; i++) {
		ws_setConsoleColors((ws_color_t) { WS_FG_BRIGHT_GREEN, WS_BG_DEFAULT });
		if (commands[i].command_name && ws_internal_startsWith(commands[i].command_name, str)) {
			printf("\t%s\n", commands[i].command_name);
		}

		// Check aliases for a match
		for (uint8_t alias_idx = 0; alias_idx < commands[i].alias_count; alias_idx++) {
			if (commands[i].aliases[alias_idx] && ws_internal_startsWith(commands[i].aliases[alias_idx], str)) {
				printf("\t%s (A)\n", commands[i].aliases[alias_idx]);
			}
		}
		ws_setConsoleColors(ws_getDefaultColors());
	}
}

/**
 * @internal
 * @brief Finds a registered command by name or alias. Returns NULL if none match.
 */
const ws_command_t* ws_internal_findCommand(const char* name) {
	for (size_t i = 0; i < current_command_spot; i++) {
		if (commands[i].command_name && strcmp(commands[i].command_name, name) == 0) return &commands[i];
		for (uint8_t alias_idx = 0; alias_idx < commands[i].alias_count; alias_idx++) {
			if (commands[i].aliases[alias_idx] && strcmp(commands[i].aliases[alias_idx], name) == 0) return &commands[i];
		}
	}
	return NULL;
}

/**
 * @internal
 * @brief Help function main command
 */
int helpMain(int argc, char** argv) {
	ws_context_t* ctx = ws_getCurrentContext();
	if (!ws_parse_args(ctx, argc, argv)) return 1;

	if (ws_has_arg(ctx, "search")) {
		helpSearch(ws_get_string(ctx, "search"));
		return 0;
	}

	if (ws_has_arg(ctx, "command")) {
		const char* name = ws_get_generic(ctx, "command");
		const ws_command_t* found = ws_internal_findCommand(name);
		if (!found) {
			ws_internal_argError("Help command not found for: %s", name);
			return 0;
		}
		ws_internal_showCommandHelp(found, argc - 1, argv + 1);
		return 0;
	}

	printf("\n");
	ws_setConsoleColors((ws_color_t) { WS_FG_CYAN, WS_BG_DEFAULT });
	printf("To get more info about a command, run `help <command_name>`\n");
	ws_setConsoleColors((ws_color_t) { WS_FG_YELLOW, WS_BG_DEFAULT });
	printf("All commands:\n");

	ws_setConsoleColors((ws_color_t) { WS_FG_BRIGHT_GREEN, WS_BG_DEFAULT });
	// List all available commands
	for (int i = 0; i < current_command_spot; i++) {
		if (commands[i].command_name) {
			printf("  %s\n", commands[i].command_name);
		}
	}
	printf("\n");
	ws_setConsoleColors(ws_getDefaultColors());
	return 0;
}

/* Internal history command */
const char* history_aliases[] = { "hist" };

/**
 * @internal
 * @brief History function main command
 */
int historyMain(void) {
	ws_setConsoleColors((ws_color_t) { WS_FG_YELLOW, WS_BG_DEFAULT });
	for (size_t i = 0; i < previous_commands_size; i++) {
		printf("%s\n", previousCommands[i]);
	}
	ws_setConsoleColors(ws_getDefaultColors());
	return 0;
}

#ifdef THREADED_SUPPORT
ws_atomic_bool_t* exit_terminal = NULL;

/**
 * @internal
 * @brief Checks exit bool, makes sure it exists.
 */
void ws_internal_checkExitBool() {
	if (!exit_terminal) exit_terminal = ws_createAtomicBool(false);
}
#define CHECK_EXIT_BOOL_EXISTS ws_internal_checkExitBool()
#define GET_EXIT_BOOL ws_getAtomicBool(exit_terminal)
#define SET_EXIT_BOOL(b) ws_setAtomicBool(exit_terminal, b)

/**
 * @brief Stops the currently running terminal. Only supported in threaded applications.
 */
void ws_stopTerminal() { SET_EXIT_BOOL(true); }
#else
bool exit_terminal = false;
#define CHECK_EXIT_BOOL_EXISTS
#define GET_EXIT_BOOL exit_terminal
#define SET_EXIT_BOOL(b) exit_terminal = b
#endif

/* Internal exit command */
const ws_command_argument_t exit_args[] = {
	{ WS_ARG_TYPE_FLAG, false, "--yes", "-y", "Exits the terminal without the confirmation prompt." },
};

/**
 * @internal
 * @brief Exit function main command
 */
int exitMain(int argc, char** argv) {
	ws_context_t* ctx = ws_getCurrentContext();
	if (!ws_parse_args(ctx, argc, argv)) return 1;

	if (ws_get_flag(ctx, "yes")) {
		SET_EXIT_BOOL(true);
	} else {
		SET_EXIT_BOOL(ws_promptUser("Are you sure you want to exit?"));
	}
	printf("\n");
	return 0;
}

/**
 * @internal
 * @brief Registers all base commands.
 */
void ws_internal_registerBasicCommands() {
	// We static define the aliases for basic commands.
	// We dont use malloc because we dont want to deal with having to free anything
	// It also would be way messier for DISABLE_MALLOC if we did allocate things.
	// WallShell wants just the pointers, cleaning it up is the user's responsibility

	// a bare shell only has help, exit, clear, and history
	// might come up with some more overtime, such as echo, but it's not a big priority.
#ifndef NO_CLEAR_COMMAND
	ws_registerCommand((ws_command_t) {
		.command_name = "clear",
			.aliases = clear_aliases,
			.alias_count = 2,
			.main_void = clearMain,
	});
#endif // NO_CLEAR_COMMAND

#ifndef NO_HELP_COMMAND
	ws_registerCommand((ws_command_t) {
		.command_name = "help",
			.main_func = helpMain,
			.arguments = help_args,
			.arguments_count = sizeof(help_args) / sizeof(help_args[0]),
	});
#endif // NO_HELP_COMMAND

#ifndef NO_HISTORY_COMMAND
	ws_registerCommand((ws_command_t) {
		.command_name = "history",
			.aliases = history_aliases,
			.alias_count = 1,
			.main_void = historyMain,
	});
#endif // NO_HISTORY_COMMAND

#ifndef NO_EXIT_COMMAND
	ws_registerCommand((ws_command_t) {
		.command_name = "exit",
			.main_func = exitMain,
			.arguments = exit_args,
			.arguments_count = sizeof(exit_args) / sizeof(exit_args[0]),
	});
#endif // NO_EXIT_COMMAND
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Virtual Sequences and Cursor Control
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/* Input */
typedef enum {
	NONE = 0,
	CURSOR,
	FUNCTION,
} input_type_t;

#ifndef CUSTOM_CURSOR_CONTROL
/**
 * @brief Move the cursor n times in the provided direction.
 * @param direction Direction to move the cursor, using ws_cursor_t.
 * @param n Amount of times to move in that direction.
 */
void ws_moveCursor_n(ws_cursor_t direction, size_t n) {
	switch (direction) {
		case WS_CURSOR_LEFT: {
				printf("\033[%zuD", n);
				break;
			}
		case WS_CURSOR_RIGHT: {
				printf("\033[%zuC", n);
				break;
			}
		case WS_CURSOR_UP: {
				printf("\033[%zuA", n);
				break;
			}
		case WS_CURSOR_DOWN: {
				printf("\033[%zuB", n);
				break;
			}
		default: break;
	}
}

/**
 * @brief Moves the cursor once in the provided direction.
 * @param direction Direction to move, using ws_cursor_t.
 */
void ws_moveCursor(ws_cursor_t direction) { ws_moveCursor_n(direction, 1); }
#endif // CUSTOM_CURSOR_CONTROL

typedef struct {
	input_type_t type;
	uint64_t result;
} input_result_t;

/**
 * @internal
 * @brief Processes a virtual terminal sequence
 *
 * @return input_result_t The type of input that the sequence was.
 */
input_result_t ws_internal_processVirtualSequence() {
	// The next character should be '[', and we can parse input until we know it should end with a certain character.
	// For simplicity's sake we're just going to preallocate a buffer for the input
	// If it doesn't end up being used it's not a big deal.
	input_result_t result = { NONE, 0 };
	int next = ws_internal_get_char_blocking();
	if (next != '[' && next != 'O') {
		printf("%c", next);
		return result;
	}

	char seq[10];
	int i = 0;

	// Read until we encounter a non-numeric character
	next = ws_internal_get_char_blocking();
	while (next >= '0' && next <= '9' || next == ';') {
		seq[i++] = (char) next;
		next = ws_internal_get_char_blocking();
	}
	seq[i] = '\0';

	// Handle the end character of the escape sequence
	switch (next) {
		case 'A': result.type = CURSOR;
			result.result = WS_CURSOR_UP;
			break;
		case 'B': result.type = CURSOR;
			result.result = WS_CURSOR_DOWN;
			break;
		case 'C': result.type = CURSOR;
			result.result = WS_CURSOR_RIGHT;
			break;
		case 'D': result.type = CURSOR;
			result.result = WS_CURSOR_LEFT;
			break;
			//case '~': printf("Function key, sequence: %s\n", seq);
			//	break;
			//case 'P':
			//case 'Q':
			//case 'R':
			//case 'S': printf("Special function key\n");
			//	break;
		default: break;
	}
	return result;
}

/**
 * @internal
 * @brief Process E0 keys. This is mostly for arrow keys in custom OS's and Windows.
 *
 * @return input_result_t
 */
input_result_t ws_internal_processEO() {
	// Up: 0x48 -> Down: 0x50 -> Right: 0x4d -> Left: 0x4b
	int next = ws_internal_get_char_blocking();;
	input_result_t result = { NONE, 0 };
	switch (next) {
		case WS_CURSOR_UP:
		case WS_CURSOR_DOWN:
		case WS_CURSOR_LEFT:
		case WS_CURSOR_RIGHT: result.type = CURSOR;
			result.result = next;
			break;
		default: break;
	}
	return result;
}

char** envp = NULL;

void ws_setEnvironment(char** e) {
	envp = e;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Execute command & Main
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

#ifdef DISABLE_MALLOC
/**
 * @brief Parse the command buffer into argv, supporting quoted args, in place with max_args.
 */
int ws_internal_parseArgsForExecute(char* commandBuf, char** argv, int max_args) {
	int argc = 0;
	char* read_ptr = commandBuf;
	char* write_ptr = commandBuf;
	bool in_quotes = false;
	bool in_arg = false;

	while (*read_ptr != '\0') {
		if (isspace((unsigned char) *read_ptr) && !in_quotes) {
			if (in_arg) {
				*write_ptr++ = '\0';
				in_arg = false;
			}
		} else if (*read_ptr == '"') {
			in_quotes = !in_quotes;

			// If starting a new argument with a quote
			if (!in_arg && argc < max_args - 1) {
				argv[argc++] = write_ptr;
				in_arg = true;
			}
		} else {
			// If starting a new unquoted argument
			if (!in_arg && argc < max_args - 1) {
				argv[argc++] = write_ptr;
				in_arg = true;
			}
			*write_ptr++ = *read_ptr; // Shift character to the left
		}
		read_ptr++;
	}

	// Terminate the final argument if the string didn't end in a space
	if (in_arg) {
		*write_ptr = '\0';
	}

	argv[argc] = NULL; // Null-terminate the array for safety
	return argc;
}
#endif

#ifndef DISABLE_MALLOC
/**
 * @brief Parses commandBuf with quoted string support using dynamic memory allocation. Allows an unbounded number of arguments.
 */
static ws_error_t ws_internal_parseArgsForExecuteAlloc(char* commandBuf, int* out_argc, char*** out_argv) {
	int argc = 0;
	char** argv = NULL;
	char* read_ptr = commandBuf;
	char* write_ptr = commandBuf;
	bool in_quotes = false;
	bool in_arg = false;
	char* current_arg = NULL;

	while (*read_ptr != '\0') {
		if (isspace((unsigned char) *read_ptr) && !in_quotes) {
			if (in_arg) {
				*write_ptr++ = '\0';
				in_arg = false;

				char** newptr = (char**) realloc(argv, sizeof(char*) * (argc + 1));
				if (!newptr) goto alloc_error;
				argv = newptr;

				char* str = (char*) malloc(strlen(current_arg) + 1);
				if (!str) goto alloc_error;
				strcpy(str, current_arg);
				argv[argc++] = str;
			}
		} else if (*read_ptr == '"') {
			in_quotes = !in_quotes;
			if (!in_arg) {
				current_arg = write_ptr;
				in_arg = true;
			}
		} else {
			if (!in_arg) {
				current_arg = write_ptr;
				in_arg = true;
			}
			*write_ptr++ = *read_ptr;
		}
		read_ptr++;
	}

	if (in_arg) {
		*write_ptr = '\0';
		char** newptr = (char**) realloc(argv, sizeof(char*) * (argc + 1));
		if (!newptr) goto alloc_error;
		argv = newptr;

		char* str = (char*) malloc(strlen(current_arg) + 1);
		if (!str) goto alloc_error;
		strcpy(str, current_arg);
		argv[argc++] = str;
	}

	// NULL-terminate the argv array
	if (argc > 0) {
		char** newptr = (char**) realloc(argv, sizeof(char*) * (argc + 1));
		if (!newptr) goto alloc_error;
		argv = newptr;
		argv[argc] = NULL;
	}

	*out_argc = argc;
	*out_argv = argv;
	return WS_NO_ERROR;

alloc_error:
	if (argv) {
		for (int i = 0; i < argc; i++) free(argv[i]);
		free(argv);
	}
	return WS_OUT_OF_MEMORY;
}
#endif // DISABLE_MALLOC

static bool ws_internal_hasUnmatchedQuotes(const char* str) {
	if (!str) return false;
	int quote_count = 0;
	while (*str) {
		if (*str == '"') {
			quote_count++;
		}
		str++;
	}
	return (quote_count % 2) != 0;
}

/**
 * @brief Execute a command with the provided buffer.
 * @param commandBuf Buffer containing the command to execute.
 * @return WS_OUT_OF_MEMORY if malloc/realloc fails, or WS_NO_ERROR.
 */
ws_error_t ws_executeCommand(char* commandBuf) {
	// Before we even parse anything, we need to make sure we don't have an odd number of quotes
	// Having an odd number means the user didn't close a quote somewhere
	if (ws_internal_hasUnmatchedQuotes(commandBuf)) {
		ws_setConsoleColors((ws_color_t) { WS_FG_BRIGHT_RED, WS_BG_DEFAULT });
		printf("Syntax error: Unmatched double quote '\"'.\n");
		ws_setConsoleColors(ws_getDefaultColors());
		return WS_UNBALANCED_QUOTES;
	}
#ifdef DISABLE_MALLOC
	int argc = 0;
	char* argv[MAX_ARGS];
	argc = ws_internal_parseArgsForExecute(commandBuf, argv, MAX_ARGS);
#else
	int argc = 0;
	char** argv = NULL;
	ws_error_t err = ws_internal_parseArgsForExecuteAlloc(commandBuf, &argc, &argv);
	if (err != WS_NO_ERROR) {
		return err;
	}
#endif // DISABLE_MALLOC

	if (argc == 0) {
		// Empty command buffer passed
#ifndef DISABLE_MALLOC
		if (argv) free(argv);
#endif
		return WS_NO_ERROR;
	}

	// Call Command (if it exists)

	const ws_command_t* found = ws_internal_findCommand(argv[0]);
	if (found) {
		CONTEXT_MUTEX_CHECK;
		LOCK_CONTEXT_MUTEX;

		if (ws_internal_wantsHelp(argc, argv)) {
			ws_internal_context_reset(&ws_internal_current_context, found);
			ws_internal_showCommandHelp(found, argc, argv);
		} else {
			ws_internal_context_reset(&ws_internal_current_context, found);

			int result;
			if (found->env_func) {
				result = found->env_func(argc, argv, envp);
			} else if (found->main_func) {
				result = found->main_func(argc, argv);
			} else if (found->main_void) {
				result = found->main_void();
			} else {
				result = 0;
			}

			if (result != 0) {
				// If the command function returns a non-zero value, it may indicate an error
				ws_setConsoleColors((ws_color_t) { WS_FG_BRIGHT_RED, WS_BG_DEFAULT });
				printf("Command exited with code: %d\n", result);
			}
		}

		ws_internal_context_reset(&ws_internal_current_context, NULL);
		UNLOCK_CONTEXT_MUTEX;
		goto cleanup;
	}

	ws_setConsoleColors((ws_color_t) { WS_FG_BRIGHT_RED, WS_BG_DEFAULT });
	printf("Command not found: \"%s\"\n", argv[0]);

cleanup:
#ifndef DISABLE_MALLOC
	for (int i = 0; i < argc; i++) free(argv[i]);
	free(argv);
#endif // DISABLE_MALLOC
	ws_setConsoleColors(ws_getDefaultColors());
	return WS_NO_ERROR;
}

// Default prefix
const char* prefix = "> ";
/**
 * @brief Set the prefix to the provided one.
 *
 * The prefix is what is displayed at the start of a command line.
 * It is possible to use this function to imitate a bash like `user@name:path$`, or any other combination.
 *
 * @param newPrefix
 */
void ws_setConsolePrefix(const char* newPrefix) { prefix = newPrefix; }

#include <acpi/acpi_api.h>
#include <system/timer.h>

/**
 * @brief Cleans everything.
 *
 * Resets everything to it's default state, frees all allocations, etc.
 * Ideally you should call this before you exit, but the system garbage collector should clean it up.
 * Don't rely on the system gc for critical applications.
 */
void ws_cleanAll() {
	prefix = "> ";
	backspace_as_ascii_delete = false;
#ifdef THREADED_SUPPORT
	if (exit_terminal) ws_destroyAtomicBool(exit_terminal);
	exit_terminal = NULL;
#else
	exit_terminal = false;
#endif // THREADED_SUPPORT
	ws_internal_cleanStreams();
	ws_internal_cleanCommands();
	ws_internal_cleanContext();
	ws_internal_cleanColors();
#ifndef NO_LOGGING
	ws_internal_cleanLogger();
#endif // NO_LOGGING
	// ws_resetConsoleState();
}

/**
 * @brief Main function for the terminal. Call after any configuration.
 * @return Can return WS_OUT_OF_MEMORY if DISABLE_MALLOC is not defined, and malloc returns NULL.
 */
ws_error_t ws_terminalMain() {
	/* We're assuming that the user has printed everything they want prior to calling main. */
	/* We're also assuming the colors have been defined, even if they are blank. */
#ifndef NO_BASIC_COMMANDS
	ws_internal_registerBasicCommands();
#endif

	// // Check for stream configurations
	// if (!ws_err_stream) ws_setStream(WS_ERROR_S, stderr);
	// if (!ws_out_stream) ws_setStream(WS_OUTPUT, stdout);
	// if (!ws_in_stream) ws_setStream(WS_INPUT, stdin);

#ifndef CUSTOM_WS_SETUP
	ws_internal_setConsoleMode();
#endif // CUSTOM_WS_SETUP

	// Make sure the colors are set properly if they are defaults
	ws_internal_updateColors();

	printf_color(PRINT_COLOR_PINK, PRINT_DEFAULT_BG, "Initializing terminal...");
	busy_wait_ms(1000);
	ws_executeCommand("clear");
	ws_executeCommand("logo");

	/* Ideally something should've caught this before calling main, but we still need to check. */
#ifndef DISABLE_MALLOC
	if (!commands) commands = malloc(sizeof(ws_command_t));
	if (!commands) return WS_OUT_OF_MEMORY;
#endif
	bool newCommand = true;
	bool tabPressed = false; // allows for autocompletion

	int history_index = -1;
	size_t current_position = 1;

	char commandBuf[MAX_COMMAND_BUF];
	char oldCommand[MAX_COMMAND_BUF];

	input_result_t input_result = { 0, 0 };
	CHECK_EXIT_BOOL_EXISTS;
	while (!GET_EXIT_BOOL) {
		if (newCommand) {
			printf("%s", prefix);
			newCommand = false;
			tabPressed = false;
			history_index = -1;
			current_position = 1;
			memset(oldCommand, 0, MAX_COMMAND_BUF);
			memset(commandBuf, 0, MAX_COMMAND_BUF);
#ifdef PRINTING_NEEDS_FLUSH
			fflush(ws_out_stream);
#endif
		}

		// Check for the previous input results
		if (input_result.type != NONE) {
			if (input_result.type == CURSOR) {
				switch (input_result.result) {
					case WS_CURSOR_UP: {
							if (previous_commands_size > 0) {
								if (history_index == -1) {
									// Save whatever the user typed before entering history
									memset(oldCommand, 0, MAX_COMMAND_BUF);
									memcpy(oldCommand, commandBuf, MAX_COMMAND_BUF);
									history_index = 0;
								} else if (history_index < (int) previous_commands_size - 1) {
									history_index++;
								}

								CLEAR_ROW;
								memset(commandBuf, 0, MAX_COMMAND_BUF);
								memcpy(commandBuf, previousCommands[history_index], strlen(previousCommands[history_index]));
								printf("\r%s%s", prefix, commandBuf);
								current_position = strlen(commandBuf) + 1;
							}
							input_result.type = NONE;
							continue;
						}
					case WS_CURSOR_DOWN: {
							if (history_index != -1) {
								CLEAR_ROW;
								if (history_index > 0) {
									history_index--;
									memset(commandBuf, 0, MAX_COMMAND_BUF);
									memcpy(commandBuf, previousCommands[history_index], strlen(previousCommands[history_index]));
								} else {
									// Returned to the original uncommitted command
									history_index = -1;
									memset(commandBuf, 0, MAX_COMMAND_BUF);
									memcpy(commandBuf, oldCommand, MAX_COMMAND_BUF);
								}
								printf("\r%s%s", prefix, commandBuf);
								current_position = strlen(commandBuf) + 1;
							}
							input_result.type = NONE;
							continue;
						}
					case WS_CURSOR_RIGHT: {
							if (current_position == (strlen(commandBuf) + 1)) break;
							current_position++;
							ws_moveCursor(WS_CURSOR_RIGHT);
							input_result.type = NONE;
							continue;
						}
					case WS_CURSOR_LEFT: {
							if (current_position == 1) break;
							current_position--;
							ws_moveCursor(WS_CURSOR_LEFT);
							input_result.type = NONE;
							continue;
						}
					default: break;
				}
			}
#ifdef PRINTING_NEEDS_FLUSH
			fflush(ws_out_stream);
#endif
		}

		int current = ws_internal_get_char_nonblocking(ws_in_stream);

		if (current == -2) {
			acpi_poll_events();
			busy_wait_ms(1);

			continue;
		}

		if (backspace_as_ascii_delete && current == 0x7f)
			current = '\b';

		if (current == '\n' || current == '\r') {
			printf("\n");
			if (strlen(commandBuf) == 0) {
				newCommand = true;
				continue;
			}

			if (previous_commands_size > 0) {
				if (strcmp(previousCommands[0], commandBuf) != 0) {
					for (size_t i = previous_commands_size; i > 0; i--) {
						memcpy(previousCommands[i], previousCommands[i - 1], strlen(previousCommands[i - 1]));
						memset(previousCommands[i - 1], 0, MAX_COMMAND_BUF);
					}

					if (previous_commands_size < PREVIOUS_BUF_SIZE) {
						previous_commands_size++;
					}
				}
				memcpy(previousCommands[0], commandBuf, strlen(commandBuf));
			} else {
				previous_commands_size++;
				memcpy(previousCommands[0], commandBuf, strlen(commandBuf));
			}
			ws_executeCommand(commandBuf);
			commandBuf[0] = '\0';
			newCommand = true;
		} else if (current == '\b') {
			if (strlen(commandBuf) > 0) {
				if (current_position <= 1) continue;

				size_t len = strlen(commandBuf);
				for (size_t i = current_position - 2; i < len; i++) {
					commandBuf[i] = commandBuf[i + 1];
				}
				commandBuf[len - 1] = '\0';

				current_position--;
				if (current_position != (strlen(commandBuf) + 1)) {
					CLEAR_ROW;
					printf("\r%s%s", prefix, commandBuf);

					// Position cursor correctly after redrawing from column 0
					for (size_t i = strlen(commandBuf) + 1; i > current_position; i--) {
						ws_moveCursor(WS_CURSOR_LEFT);
					}
				} else {
					ws_moveCursor(WS_CURSOR_LEFT);
					printf(" ");
					ws_moveCursor(WS_CURSOR_LEFT);
				}
			}
		} else if (current == '\t') {
			const char* list[50];
			int list_size = 0;
			for (int i = 0; i < command_size; i++) {
				ws_setConsoleColors((ws_color_t) { WS_FG_BRIGHT_GREEN, WS_BG_DEFAULT });
				if (commands[i].command_name && ws_internal_startsWith(commands[i].command_name, commandBuf)) {
					list[list_size] = commands[i].command_name;
					list_size++;
				}

				for (uint8_t alias_idx = 0; alias_idx < commands[i].alias_count; alias_idx++) {
					if (commands[i].aliases[alias_idx] && ws_internal_startsWith(commands[i].aliases[alias_idx], commandBuf)) {
						bool already_in_list = false;
						for (int j = 0; j < list_size; j++) {
							if (strcmp(list[j], commands[i].command_name) == 0) {
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
				ws_setConsoleColors(ws_getDefaultColors());
			}

			if (list_size == 1) {
				size_t len = strlen(commandBuf);
				const char* currentCommand = list[0];
				for (size_t i = len; i < strlen(currentCommand); i++) {
					printf("%c", currentCommand[i]);
					ws_internal_strcat_c(commandBuf, currentCommand[i], MAX_COMMAND_BUF);
				}
				current_position = strlen(commandBuf) + 1;
				tabPressed = false;
			} else if (tabPressed) {
				if (list_size == 0) {
					ws_setConsoleColors((ws_color_t) { WS_FG_BRIGHT_RED, WS_BG_DEFAULT });
					printf("\nNo command starting with: %s\n", commandBuf);
					memset(commandBuf, 0, MAX_COMMAND_BUF * sizeof(char));
					commandBuf[0] = '\0';
					newCommand = true;
				} else if (list_size > 1) {
					ws_setConsoleColors((ws_color_t) { WS_FG_YELLOW, WS_BG_DEFAULT });
					printf("\n");
					for (int i = 0; i < list_size; i++) {
						printf("%s\n", list[i]);
					}
					ws_setConsoleColors(ws_getDefaultColors());
					printf("\r%s%s", prefix, commandBuf);

					// Reposition cursor if tab completion ran mid-line
					for (size_t i = strlen(commandBuf) + 1; i > current_position; i--) {
						ws_moveCursor(WS_CURSOR_LEFT);
					}
				}
				tabPressed = false;
			} else {
				tabPressed = true;
			}
			ws_setConsoleColors(ws_getDefaultColors());
		} else if (current == EOF) {
			break;
		} else if (current == '\033') {
			input_result = ws_internal_processVirtualSequence();
		} else if (current == 0xE0) {
			input_result = ws_internal_processEO();
		} else {
			ws_internal_insert_c(commandBuf, MAX_COMMAND_BUF, (char) current, current_position);
			if (current_position != strlen(commandBuf)) {
				CLEAR_ROW;
				printf("\r%s%s", prefix, commandBuf);
				for (size_t i = strlen(commandBuf) + 1; i > current_position + 1; i--) {
					ws_moveCursor(WS_CURSOR_LEFT);
				}
			} else {
				printf("%c", current);
			}
			current_position++;
		}
#ifdef PRINTING_NEEDS_FLUSH
		fflush(ws_out_stream);
#endif
	}
	return WS_NO_ERROR;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// General Utility functions
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/**
 * @brief Operator overloading isn't available in C. Compare two commands using this.
 *
 * @param c1 Command to compare.
 * @param c2 Command to compare.
 * @return true If they are the same.
 * @return false If they are different.
 */
bool ws_compareCommands(const ws_command_t c1, const ws_command_t c2) {
	if (c1.aliases != c2.aliases) return false;
	if (c1.alias_count != c2.alias_count) return false;
	if (strcmp(c1.command_name, c2.command_name) != 0) return false;
	if (c1.help_func != c2.help_func) return false;
	if (c1.main_void != c2.main_void) return false;
	if (c1.main_func != c2.main_func) return false;
	if (c1.env_func != c2.env_func) return false;
	if (c1.arguments != c2.arguments) return false;
	if (c1.arguments_count != c2.arguments_count) return false;
	return true;
}

/**
 * @brief Sets the console locale. This is only required on Windows systems, as terminals default to ASCII.
 *
 * The built in SET_TERMINAL_LOCALE sets the Windows terminal to UTF8.
 * Can be potentially be used on unix systems to configure locale, although this isn't done by default.
 * If the system you are implementing requires locale configuration, redefine SET_TERMINAL_LOCALE to the needed configuration.
 */
void ws_setConsoleLocale() { SET_TERMINAL_LOCALE; }

/**
 * @brief Prompts the user yes/no using the given prompt.
 * @param format Printf style formatting string.
 * @param ... Printf style arguments.
 * @return True if the user reply's yes, false otherwise. Will return false if the user enters anything other than something starting with 'Y' or 'y'.
 */
bool ws_promptUser(const char* format, ...) {
	va_list arg;
	va_start(arg, format);
	vprintf(format, arg);
	va_end(arg);

	printf(" [Y/n] ");
	int first_input = ws_internal_get_char_blocking();
	printf("%c", first_input);
	int input;
	do {
		input = ws_internal_get_char_blocking();
		printf("%c", input);
	} while (input != '\n' && input != '\r');
	if (first_input == 'Y' || first_input == 'y') return true;
	return false;
}