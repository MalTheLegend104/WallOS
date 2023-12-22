#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <klibc/kprint.h>
static const size_t vga_width = 80;
static const size_t vga_height = 25;

size_t cursor_row = 0;
size_t cursor_col = 0;

uint8_t text_colors = VGA_COLOR_LIGHT_GREY;
int8_t 	background = VGA_COLOR_BLACK;

uint8_t last_text;
int8_t 	last_bg;

uint16_t* screen_buffer = (uint16_t*) 0xB8000; // location of screen memory

/* These three functions do exactly what they say. https://wiki.osdev.org/Text_Mode_Cursor */
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

/**
 * @brief Set the colors for the text to be displayed.
 *
 * @param text Color for the text.
 * @param back Color for the background.
 */
void set_colors(char text, char back) {
	last_text = text_colors;
	last_bg = background;
	text_colors = text;
	background = back;
}


void set_colors_default() {
	last_text = text_colors;
	last_bg = background;
	text_colors = VGA_DEFAULT_FG;
	background = VGA_DEFAULT_BG;
}

void set_to_last() {
	text_colors = last_text;
	background = last_bg;
}

/* Formats the character to correctly display with the selected colors. */
uint16_t format_char_data(unsigned char c) {
	// add an exception for our logo

	uint8_t colors = (background << 4) | text_colors;

	uint16_t colored_char = ((uint16_t) colors << 8 | (uint16_t) c);
	return colored_char;
}

/* Places the character at the provided location..... */
void place_char_at_location(unsigned char c, size_t x, size_t y) {
	screen_buffer[(x * vga_width) + y] = format_char_data(c); // put the char at the location
}

/* It clears the provided row... */
void clear_row(size_t row) {
	for (size_t col = 0; col < vga_width; col++) {
		screen_buffer[col + vga_width * row] = format_char_data(' ');
	}
}

/**
 * @brief Clears the current row of the console.
 * Leaves cursor where it is. To reset cursor print a carriage return ('\r')
 */
void clear_current_row() {
	clear_row(cursor_row);
}

/* Well... it scrolls the screen. What else were you expecting? */
void scroll_screen() {
	for (size_t i = 0; i < vga_height; i++) {
		memcpy(screen_buffer + (vga_width * i), screen_buffer + (vga_width * (i + 1)), vga_width * 2);
	}
	short c = format_char_data(' ');
	memsetw(screen_buffer + (vga_width * vga_height), c, vga_width);
}

/**
 * @brief Unfiltered putc. This is so we have access to certain characters,
 * like the circle from 0x09 and the smily from 0x01. These would be
 * filtered out of the normal putc. (They're control characters.)
 * https://theasciicode.com.ar/ascii-control-characters/start-of-header-ascii-code-1.html
 * @param c Character to print.
 */
void putc_vga_unfiltered(const unsigned char c) {
	if ((cursor_col > vga_width - 1) || (c == '\n')) {
		if (cursor_row >= vga_height - 1) {
			scroll_screen();
		} else {
			cursor_row++;
		}

		cursor_col = 0;
		place_char_at_location(c, cursor_row, cursor_col);
	} else if (cursor_row > vga_height - 1) {
		scroll_screen();
		place_char_at_location(c, cursor_row, cursor_col);
	} else {
		place_char_at_location(c, cursor_row, cursor_col);
	}

	cursor_col++;
	update_cursor(cursor_row, cursor_col);
}

/**
 * @brief Normal plain ole putc. Handles backspace & other control
 * characters. Keeps track of carrige returns and scrolling the screen.
 *
 * @param c Character to printed.
 */
void putc_vga(const unsigned char c) {
	// If char is null terminator, we just return.
	if (c == '\0') return;

	// Backspace
	// We will return from here so the rest of the function is left alone.
	if (c == '\b') {
		if (cursor_col == 0) {
			// Beginning of next row, we have to back to the last row.
			cursor_col = vga_width - 1;
			cursor_row -= 1;
			place_char_at_location(' ', cursor_row, cursor_col);
		} else {
			// Same row
			cursor_col--;
			place_char_at_location(' ', cursor_row, cursor_col);
		}

		update_cursor(cursor_row, cursor_col);
		return;
	}

	if (c == '\r') {
		cursor_col = 0;
		update_cursor(cursor_row, cursor_col);
		return;
	}

	// Everything else
	if ((cursor_col > vga_width - 1) || (c == '\n')) {
		if (cursor_row >= vga_height - 1) {
			scroll_screen();
		} else {
			cursor_row++;
		}

		cursor_col = 0;
		if (c != '\n' && c != '\t') {
			// Text wrap
			cursor_col = 1;
			place_char_at_location(c, cursor_row, cursor_col);
		}
	} else if (cursor_row > vga_height - 1) {
		scroll_screen();
		place_char_at_location(c, cursor_row, cursor_col);
	} else if (c == '\t') {
		// This is cursed. This gives tab 4 spaces. IDK, dont ask.
		putc_vga(' ');
		putc_vga(' ');
		putc_vga(' ');
	} else {
		place_char_at_location(c, cursor_row, cursor_col);
	}

	cursor_col++;
	update_cursor(cursor_row, cursor_col);
}

/**
 * @brief The normal puts(), although for the vga text buffer.
 *
 * @param buf Text to be printed.
 */
void puts_vga(const char* buf) {
	for (size_t i = 0; i < strlen(buf); i++) {
		putc_vga(buf[i]);
	}
}

/**
 * @brief Puts but for uint8_t arrays (unsigned chars) instead of normal strings.
 *
 * @param buf the array to be printed.
 */
void putuc_vga(const uint8_t* buf, size_t size) {
	for (size_t i = 0; i < size; i++) {
		putc_vga(buf[i]);
	}
}

/**
 * @brief Clears the entire textbuffer.
 * TAKE NOTE: it will use whatever color background is set.
 * It WILL NOT make entire screen black.
 */
void clearVGABuf() {
	enable_cursor(0, 25);
	update_cursor(0, 0);
	unsigned short c = format_char_data(' ');
	for (size_t i = 0; i < vga_width * vga_height; i++) {
		screen_buffer[i] = c;
	}
	cursor_row = 0;
	cursor_col = 0;
}

/**
 * @brief Centers the provided buffer on the screen
 *
 * @param buf text to be centered
 */
void center_text(const char* buf) {
	size_t size = strlen(buf);
	size_t index = 0;

	cursor_col = 0;

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
	for (size_t i = cursor_row; i < vga_width; i++) {
		putc_vga(' ');
	}
}

void pink_screen_sa(const char** error, uint8_t length) {
	disable_cursor();
	set_colors(VGA_COLOR_WHITE, VGA_COLOR_PINK);
	cursor_row = 0;
	cursor_col = 0;
	for (size_t i = cursor_col; i < vga_width; i++) {
		putc_vga(' ');
	}

	// Header text
	cursor_row = 1;
	cursor_col = 0;
	center_text("Kernel Panic!");
	cursor_col = 0;
	for (size_t i = cursor_col; i < vga_width; i++) {
		putc_vga(' ');
	}

	if (length > (vga_height - 3)) length = vga_height - 3;

	// Body text
	cursor_row++;
	cursor_col = 0;
	for (int i = 0; i < length; i++) {
		center_text(error[i]);
		for (size_t i = cursor_col; i < vga_width; i++) {
			putc_vga(' ');
		}
	}
	cursor_col = 0;
	while (cursor_row < vga_height) {
		for (size_t i = cursor_col; i < vga_width; i++) {
			putc_vga(' ');
		}
		cursor_row++;
		cursor_col = 0;
	}
	asm volatile("hlt");
}

/**
 * @brief Associated with kernel panics, it makes the entire screen pink, displays "Kernel Panic!", along with the error.
 *
 * @param error Error message to be printed to the screen.
 */
void pink_screen(const char* error) {
	disable_cursor();
	set_colors(VGA_COLOR_WHITE, VGA_COLOR_PINK);
	cursor_row = 0;
	cursor_col = 0;
	for (size_t i = cursor_col; i < vga_width; i++) {
		putc_vga(' ');
	}
	// Header text
	cursor_row = 1;
	cursor_col = 0;
	center_text("Kernel Panic!");
	cursor_col = 0;
	for (size_t i = cursor_col; i < vga_width; i++) {
		putc_vga(' ');
	}

	// Body text
	cursor_row++;
	cursor_col = 0;
	center_text(error);
	cursor_col = 0;
	while (cursor_row < vga_height) {
		for (size_t i = cursor_col; i < vga_width; i++) {
			putc_vga(' ');
		}
		cursor_row++;
		cursor_col = 0;
	}
}

void puts_vga_color(const char* string, uint8_t fg, uint8_t bg) {
	set_colors(fg, bg);
	puts_vga(string);
	set_to_last();
}

/**
 * @brief Prints the logo to the screen.
 */
void print_logo() {
	// OS LOGO IGNORE THIS STUPID ARRAY
	// GCC and VSCode both try to make the normal string unicode...
	// We don't have unicode...
	uint8_t a[] = {
		0x0A, 0xdb, 0xdb, 0xbb, 0x20, 0x20, 0x20, 0x20, 0xdb, 0xdb,
		0xbb, 0x20, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xbb, 0x20, 0xdb,
		0xdb, 0xbb, 0x20, 0x20, 0x20, 0x20, 0x20, 0xdb, 0xdb, 0xbb,
		0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0xdb, 0xdb, 0xdb, 0xdb,
		0xdb, 0xdb, 0xbb, 0x20, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb,
		0xdb, 0xbb, 0x0a, 0xdb, 0xdb, 0xba, 0x20, 0x20, 0x20, 0x20,
		0xdb, 0xdb, 0xba, 0xdb, 0xdb, 0xc9, 0xcd, 0xcd, 0xdb, 0xdb,
		0xbb, 0xdb, 0xdb, 0xba, 0x20, 0x20, 0x20, 0x20, 0x20, 0xdb,
		0xdb, 0xba, 0x20, 0x20, 0x20, 0x20, 0x20, 0xdb, 0xdb, 0xc9,
		0xcd, 0xcd, 0xcd, 0xdb, 0xdb, 0xbb, 0xdb, 0xdb, 0xc9, 0xcd,
		0xcd, 0xcd, 0xcd, 0xbc, 0x0a, 0xdb, 0xdb, 0xba, 0x20, 0xdb,
		0xbb, 0x20, 0xdb, 0xdb, 0xba, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb,
		0xdb, 0xdb, 0xba, 0xdb, 0xdb, 0xba, 0x20, 0x20, 0x20, 0x20,
		0x20, 0xdb, 0xdb, 0xba, 0x20, 0x20, 0x20, 0x20, 0x20, 0xdb,
		0xdb, 0xba, 0x20, 0x20, 0x20, 0xdb, 0xdb, 0xba, 0xdb, 0xdb,
		0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xbb, 0x0a, 0xdb, 0xdb, 0xba,
		0xdb, 0xdb, 0xdb, 0xbb, 0xdb, 0xdb, 0xba, 0xdb, 0xdb, 0xc9,
		0xcd, 0xcd, 0xdb, 0xdb, 0xba, 0xdb, 0xdb, 0xba, 0x20, 0x20,
		0x20, 0x20, 0x20, 0xdb, 0xdb, 0xba, 0x20, 0x20, 0x20, 0x20,
		0x20, 0xdb, 0xdb, 0xba, 0x20, 0x20, 0x20, 0xdb, 0xdb, 0xba,
		0xc8, 0xcd, 0xcd, 0xcd, 0xcd, 0xdb, 0xdb, 0xba, 0x0a, 0xc8,
		0xdb, 0xdb, 0xdb, 0xc9, 0xdb, 0xdb, 0xdb, 0xc9, 0xbc, 0xdb,
		0xdb, 0xba, 0x20, 0x20, 0xdb, 0xdb, 0xba, 0xdb, 0xdb, 0xdb,
		0xdb, 0xdb, 0xdb, 0xdb, 0xbb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb,
		0xdb, 0xdb, 0xbb, 0xc8, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb,
		0xc9, 0xbc, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xba,
		0x0a, 0x20, 0xc8, 0xcd, 0xcd, 0xbc, 0xc8, 0xcd, 0xcd, 0xbc,
		0x20, 0xc8, 0xcd, 0xbc, 0x20, 0x20, 0xc8, 0xcd, 0xbc, 0xc8,
		0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xbc, 0xc8, 0xcd, 0xcd,
		0xcd, 0xcd, 0xcd, 0xcd, 0xbc, 0x20, 0xc8, 0xcd, 0xcd, 0xcd,
		0xcd, 0xcd, 0xbc, 0x20, 0xc8, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
		0xcd, 0xbc, 0x0a
	};
	set_colors(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
	putuc_vga(a, (sizeof(a) / sizeof(a[0])));
	set_to_last();
}
