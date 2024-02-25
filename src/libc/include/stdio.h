#ifndef _STDIO_H
#define _STDIO_H
#include <stdarg.h>
#include <stdint.h>

#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif
	extern void putc_vga(const unsigned char c);
	int vprintf(const char* format, va_list arg);
	int printf(const char* format, ...);
	int print_until_null(const char* data);
	int puts(const char* string);
	char* format_int(char* str, int size, int i);

#ifdef __cplusplus
}
#endif

#endif