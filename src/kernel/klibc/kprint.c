#include "include/klibc/kprint.h"
#include <string.h>
#include <stdlib.h>
static const size_t NUM_COLS = 80;
static const size_t NUM_ROWS = 25;

struct Char {
	uint8_t character;
	uint8_t color;
};

struct Char* buffer = (struct Char*) 0xb8000;
size_t col = 0;
size_t row = 0;
uint8_t color = VGA_COLOR_WHITE | VGA_COLOR_BLACK << 4;

void clear_row(size_t row) {
	struct Char empty = (struct Char){ ' ', color };

	for (size_t col = 0; col < NUM_COLS; col++) {
		buffer[col + NUM_COLS * row] = empty;
	}
}

/**
 * @brief Clears the entire vga buffer, effectively clearing the screen.
 *
 */
void clearVGABuf() {
	for (size_t i = 0; i < NUM_ROWS; i++) {
		clear_row(i);
	}
	row = 0;
	col = 0;
}

void print_newline() {

	for (size_t i = col; i < NUM_COLS; i++) {
		putc_vga(' ');
	}

	if (row < NUM_ROWS - 1) {
		col = 0;
		row++;
		return;
	}

	// Scrolls the buffer if the new line is at the bottom of the screen.
	for (size_t row = 1; row < NUM_ROWS; row++) {
		for (size_t col = 0; col < NUM_COLS; col++) {
			struct Char character = buffer[col + NUM_COLS * row];
			buffer[col + NUM_COLS * (row - 1)] = character;
		}
	}

	clear_row(NUM_COLS - 1);
}

/**
 * @brief The normal putc(), although for the vga text buffer.
 *
 * @param character Character to be printed.
 */
void putc_vga(char character) {
	if (character == '\n') {
		print_newline();
		return;
	}

	if (col >= NUM_COLS) {
		print_newline();
	}

	buffer[col + NUM_COLS * row] = (struct Char){ (uint8_t) character, color };

	col++;
}

/**
 * @brief The normal puts(), although for the vga text buffer.
 *
 * @param str Text to be printed.
 */
void puts_vga(const char* str) {
	for (size_t i = 0; 1; i++) {
		char character = (uint8_t) str[i];

		if (character == '\0') {
			return;
		}

		putc_vga(character);
	}
}

/**
 * @brief Set the color for the vga buffer. This lasts until this function is called with different values.
 *
 * @param foreground Color of the text. Value should be 0 < x < 16.
 * @param background Color of the background. Value should be 0 < x < 16.
 */
void set_color_vga(uint8_t foreground, uint8_t background) {
	color = foreground + (background << 4);
}

/**
 * @brief Centers the provided buffer on the screen
 *
 * @param buf text to be centered
 */
void center_text(const char* buf) {
	size_t size = strlen(buf);
	size_t index = 0;

	// If the size is greater than 80, print a whole row until it's not.
	while (size > 80) {
		for (uint8_t i = 0; i < 80; i++) {
			putc_vga(buf[i]);
		}
		size -= 80;
		index += 80;
	}

	int before = (80 - size) / 2;
	// Check if it's a whole number
	for (uint8_t i = 0; i < before; i++) {
		putc_vga(' ');
	}
	for (uint8_t i = 0; i < size; i++) {
		putc_vga(buf[index + i]);
	}
	for (size_t i = col; i < NUM_COLS; i++) {
		putc_vga(' ');
	}
}

/**
 * @brief Associated with kernel panics, it makes the entire screen pink, displays "Kernel Panic!", along with the error.
 *
 * @param error Error message to be printed to the screen.
 */
void pink_screen(const char* error) {
	clearVGABuf();
	set_color_vga(VGA_COLOR_WHITE, VGA_COLOR_MAGENTA);
	for (size_t i = col; i < NUM_COLS; i++) {
		putc_vga(' ');
	}
	row++;
	col = 0;
	center_text("Kernel Panic!\n");
	// puts_vga("Kernel Panic!");
	for (size_t i = col; i < NUM_COLS; i++) {
		putc_vga(' ');
	}
	row++;
	col = 0;
	center_text(error);
	while (row < NUM_ROWS) {
		for (size_t i = col; i < NUM_COLS; i++) {
			putc_vga(' ');
		}
		row++;
		col = 0;
	}
}