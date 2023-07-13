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

	// // write a value to a port
	// inline void outb(uint16_t port, uint8_t val) {
	// 	__asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
	// }

	// //read a value from a port
	// inline uint8_t inb(uint16_t port) {
	// 	uint8_t ret;
	// 	__asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
	// 	return ret;
	// }

#ifdef __cplusplus
}
#endif

#endif
