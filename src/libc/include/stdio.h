#ifndef _STDIO_H
#define _STDIO_H
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <print_type.h>

#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif
	int vsnprintf(char* str, size_t size, const char* format, va_list list);
	int vprintf(const char* format, va_list list);

	int printf(const char* format, ...);
	int snprintf(char* str, size_t size, const char* format, ...);
	int sprintf(char* str, const char* format, ...);

	// Custom Extensions
	size_t int_to_string(intmax_t value, int base, char* buf, size_t buflen);
	size_t uint_to_string(uintmax_t value, int base, char* buf, size_t buflen, bool capital);
	void shift_right(char* buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif