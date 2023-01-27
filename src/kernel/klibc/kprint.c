// vga_controller
// Has functions that control the screen
// It knows how to place chars on the screen
// change text color
// move the cursor

#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <klibc/kprint.h>
static const size_t vga_width = 80;
static const size_t vga_height = 25;

size_t coursor_x = 0;
size_t coursor_y = 0;

uint8_t text_colors = VGA_COLOR_LIGHT_GREY;
int8_t background = VGA_COLOR_BLACK;

uint16_t* screen_buffer = (uint16_t*) 0xB8000; // location of screen memory

VGA_COLOR default_colors = { VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK };

void set_colors(char text, char back) {
	text_colors = text;
	background = back;
}

void set_colors_(VGA_COLOR colors) {
	text_colors = colors.text_colors;
	background = colors.background;
}

void set_default() {
	text_colors = default_colors.text_colors;
	background = default_colors.background;
}

void enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
	outb(0x3D4, 0x0A);
	outb(0x3D5, (inb(0x3D5) & 0xC0) | cursor_start);

	outb(0x3D4, 0x0B);
	outb(0x3D5, (inb(0x3D5) & 0xE0) | cursor_end);
}

void update_cursor(int x, int y) {
	uint16_t pos = (x * vga_width) + y;

	outb(0x3D4, 0x0F);
	outb(0x3D5, (uint8_t) (pos & 0xFF));
	outb(0x3D4, 0x0E);
	outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

void disable_cursor() {
	outb(0x3D4, 0x0A);
	outb(0x3D5, 0x20);
}

// blends the char with the color bits that are needed for vga
uint16_t format_char_data(char c) {
	uint8_t colors = (background << 4) | text_colors;

	uint16_t colored_char = ((uint16_t) colors << 8 | (uint16_t) c);
	return colored_char;
}

// places a single character at the specified location
void place_char_at_location(char c, size_t x, size_t y) {
	screen_buffer[(x * vga_width) + y] = format_char_data(c); // put the char at the location
}

void scroll_screen() {
	for (size_t i = 0; i < vga_height; i++) {
		memcpy(screen_buffer + (vga_width * i), screen_buffer + (vga_width * (i + 1)), vga_width * 2);
	}
	short c = format_char_data(' ');
	memsetw(screen_buffer + (vga_width * vga_height), c, vga_width);
}

void puts_vga(const char* buf) {
	for (int i = 0; i < strlen(buf); i++) {
		putc_vga(buf[i]);
	}
}

void print_newline() {
	for (size_t i = coursor_x; i < vga_width; i++) {
		putc_vga(' ');
	}

	if (coursor_x < vga_height - 1) {
		coursor_y = 0;
		coursor_x++;
		return;
	}
}

// prints a single char to the screen, and keeps track of when
// there needs to be a carrage return
void putc_vga(const char c) {
	if ((coursor_y > vga_width - 1) || (c == '\n')) {
		if (coursor_x >= vga_height - 1) {
			scroll_screen();
		} else {
			coursor_x++;
		}

		coursor_y = 0;
		if (c != '\n') {
			place_char_at_location(c, coursor_x, coursor_y);
		}
	} else if (coursor_x > vga_height - 1) {
		scroll_screen();
		place_char_at_location(c, coursor_x, coursor_y);
	} else {
		place_char_at_location(c, coursor_x, coursor_y);
	}
	coursor_y++;
	update_cursor(coursor_x, coursor_y);
}

// goes through the entire screen and puts in blank spaces
void clearVGABuf() {
	enable_cursor(0, 25);
	update_cursor(0, 0);
	unsigned short c = format_char_data(' ');
	for (size_t i = 0; i < vga_width * vga_height; i++) {
		screen_buffer[i] = c;
	}
	coursor_x = 0;
	coursor_y = 0;
}

void set_text_red() {
	set_colors(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
}

void set_text_green() {
	set_colors(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
}

void set_text_blue() {
	set_colors(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
}

void set_text_grey() {
	set_colors(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void clear_row(size_t row) {
	for (size_t col = 0; col < vga_width; col++) {
		screen_buffer[col + vga_width * row] = format_char_data(' ');
	}
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
	for (size_t i = coursor_x; i < vga_width; i++) {
		putc_vga(' ');
	}
}

/**
 * @brief Associated with kernel panics, it makes the entire screen pink, displays "Kernel Panic!", along with the error.
 *
 * @param error Error message to be printed to the screen.
 */
void pink_screen(const char* error) {
	disable_cursor();
	set_colors(VGA_COLOR_WHITE, VGA_COLOR_PINK);
	coursor_x = 0;
	coursor_y = 0;
	for (size_t i = coursor_y; i < vga_width; i++) {
		putc_vga(' ');
	}

	coursor_x = 1;
	coursor_y = 0;
	center_text("Kernel Panic!");

	for (size_t i = coursor_y; i < vga_width; i++) {
		putc_vga(' ');
	}

	coursor_x++;
	coursor_y = 0;

	center_text(error);

	while (coursor_x < vga_height) {
		for (size_t i = coursor_y; i < vga_width; i++) {
			putc_vga(' ');
		}
		coursor_x++;
		coursor_y = 0;
	}

}