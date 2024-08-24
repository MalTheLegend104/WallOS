#ifndef _STDIO_H
#define _STDIO_H
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif
	extern void putc_vga(const unsigned char c);
	int vprintf(const char* format, va_list arg);
	int printf(const char* format, ...);
	int puts(const char* string);

	// Custom Extensions
	size_t int_to_string(intmax_t value, int base, char* buf, size_t buflen);
	size_t uint_to_string(uintmax_t value, int base, char* buf, size_t buflen, bool capital);
	void shift_right(char* buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif