#ifndef KLIBC_LOGGER_H
#define KLIBC_LOGGER_H
#include "kprint.h"
#include <stdarg.h>
#ifdef __cplusplus
// CPP specific logger

/*
 * We wont have timing until interrupts are enabled and APIC (or pic) is set up.
 *
 * Logging is done in the following format:
 * 		Month, Day Year HH:MM:SS [level] <message>
 * This results in messages like the following:
 * 		Jan, 29 2023 17:57:39 [error] this is an error message.
 *
 * This is the STANDARD formatting for logging in the kernel. Any process logging should be done this way.
 * This allows for reviewing log messages and log dumps on longstanding systems.
 * We want to be able to have large uptime, and this ensures that users can properly diagnose problems.
 */
namespace Logger {

	// normal log messages (like printf)
	void logf(const char* format, ...);
	void infof(const char* format, ...);
	void warnf(const char* format, ...);
	void errorf(const char* format, ...);
	void fatalf(const char* format, ...);

	// vprintf
	void vlogf(const char* format, va_list args);
	void vinfof(const char* format, va_list args);
	void vwarnf(const char* format, va_list args);
	void verrorf(const char* format, va_list args);
	void vfatalf(const char* format, va_list args);

	namespace Checklist {
		void blankEntry(const char* format, ...);
		void checkEntry(const char* format, ...);
		void noCheckEntry(const char* format, ...);
	}
}
#else
// C stuff here

#endif //__cplusplus
#endif //KLIBC_LOGGER_H