/*
 * This supports printing to vga text mode.
 * This mode supports an extention of ASCII called code page 437
 * https://en.wikipedia.org/wiki/Code_page_437
 * It supports a few extra characters such as ☺,▓, and ►
 * This allows for some interesting things.
 */

#ifndef KPRINT_H
#define KPRINT_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
	enum vga_color {
		VGA_COLOR_BLACK = 0,
		VGA_COLOR_BLUE = 1,
		VGA_COLOR_GREEN = 2,
		VGA_COLOR_CYAN = 3,
		VGA_COLOR_RED = 4,
		VGA_COLOR_PURPLE = 5,
		VGA_COLOR_BROWN = 6,
		VGA_COLOR_LIGHT_GREY = 7,
		VGA_COLOR_DARK_GREY = 8,
		VGA_COLOR_LIGHT_BLUE = 9,
		VGA_COLOR_LIGHT_GREEN = 10,
		VGA_COLOR_LIGHT_CYAN = 11,
		VGA_COLOR_LIGHT_RED = 12,
		VGA_COLOR_PINK = 13,
		VGA_COLOR_YELLOW = 14,
		VGA_COLOR_WHITE = 15,
	};

	void clearVGABuf();
	void set_colors(char text, char back);
	void set_to_last();
	void putc_vga(const unsigned char c);
	void puts_vga(const char* buf);
	void putuc_vga(const uint8_t* buf, size_t size);


	// this is temporarily here
	inline void outb(uint16_t port, uint8_t val) {
		__asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
	}

	//read a value from a port
	inline uint8_t inb(uint16_t port) {
		uint8_t ret;
		__asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
		return ret;
	}
#ifdef __is_kernel_
	void pink_screen(const char* error);
#endif // __is_kernel_
#ifdef __cplusplus
}
#endif
#endif // KPRINT_H