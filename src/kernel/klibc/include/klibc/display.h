/**
* @file display.h
* @brief Unified display abstraction layer for VGA text mode and framebuffer graphics
*
* This provides a common interface that works with both VGA text mode and framebuffer mode,
* allowing you to switch between them without refactoring application code.
*/
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <apollo.h>

#ifdef __cplusplus
extern "C" {
#endif

	// ------------------------------------------------------------------------------------------------
	// Display Mode Types
	// ------------------------------------------------------------------------------------------------
	typedef enum {
		DISPLAY_MODE_VGA_TEXT,      // VGA text mode (80x25)
		DISPLAY_MODE_FRAMEBUFFER    // Graphics framebuffer mode
	} display_mode_t;

	// ------------------------------------------------------------------------------------------------
	// Color Definition (works for both modes)
	// ------------------------------------------------------------------------------------------------
	typedef enum {
		DISPLAY_COLOR_BLACK = 0,
		DISPLAY_COLOR_BLUE = 1,
		DISPLAY_COLOR_GREEN = 2,
		DISPLAY_COLOR_CYAN = 3,
		DISPLAY_COLOR_RED = 4,
		DISPLAY_COLOR_PURPLE = 5,
		DISPLAY_COLOR_BROWN = 6,
		DISPLAY_COLOR_LIGHT_GREY = 7,
		DISPLAY_COLOR_DARK_GREY = 8,
		DISPLAY_COLOR_LIGHT_BLUE = 9,
		DISPLAY_COLOR_LIGHT_GREEN = 10,
		DISPLAY_COLOR_LIGHT_CYAN = 11,
		DISPLAY_COLOR_LIGHT_RED = 12,
		DISPLAY_COLOR_PINK = 13,
		DISPLAY_COLOR_YELLOW = 14,
		DISPLAY_COLOR_WHITE = 15,
		DISPLAY_DEFAULT_FG = 7,
		DISPLAY_DEFAULT_BG = 0
	} display_color_t;

	// ------------------------------------------------------------------------------------------------
	// Display Interface Functions
	// ------------------------------------------------------------------------------------------------

	/**
	 * @brief Initialize the display subsystem
	 * @param mode Display mode to use (VGA text or framebuffer)
	 * @return true if initialization succeeded, false otherwise
	 */
	bool display_init(display_mode_t mode);

	/**
	 * @brief Late display init that switches to a higher performance mode after all required subsystems are enabled.
	 */
	void display_init_late();
	void display_flush();

	/**
	 * @brief Get the current display mode
	 * @return Current display mode
	 */
	display_mode_t display_get_mode(void);

	/**
	 * @brief Switch display modes (if supported)
	 * @param mode New display mode
	 * @return true if switch succeeded, false otherwise
	 */
	bool display_switch_mode(display_mode_t mode);

	/**
	 * @brief Clear the entire screen
	 */
	void display_clear(void);

	/**
	 * @brief Clear the current row
	 */
	void display_clear_row(void);

	/**
	 * @brief Set foreground and background colors
	 * @param fg Foreground color
	 * @param bg Background color
	 */
	void display_set_colors(int fg, int bg);

	/**
	 * @brief Reset colors to default (light grey on black)
	 */
	void display_set_colors_default(void);

	/**
	 * @brief Print a single character
	 * @param c Character to print
	 */
	void display_putc(unsigned char c);

	/**
	 * @brief Print a single character without filtering (no newline processing)
	 * @param c Character to print
	 */
	void display_putc_unfiltered(char c);

	/**
	 * @brief Print a null-terminated string
	 * @param str String to print
	 */
	void display_puts(const char* str);

	/**
	 * @brief Print a string with specific colors
	 * @param str String to print
	 * @param fg Foreground color
	 * @param bg Background color
	 */
	void display_puts_color(const char* str, display_color_t fg, display_color_t bg);


	/* Both of these printf wrappers use ints rather than display_color_t because it makes them infinitely easier to `extern` when needed. */
	/**
	 * @brief Printf wrapper that lets you set the text colors.
	 *
	 * @param fg Foreground color
	 * @param bg Background color
	 * @param fmt Format string
	 * @param ... args
	 * @return int
	 */
	int printf_color(int fg, int bg, const char* fmt, ...);

	/**
	 * @brief vprintf wrapper that lets you set the text colors
	 *
	 * @param fg Foreground color
	 * @param bg Background color
	 * @param fmt Format string
	 * @param arg args
	 * @return int
	 */
	int vprintf_color(int fg, int bg, const char* fmt, va_list arg);

	/**
	 * @brief Enable cursor display
	 * @param cursor_start Cursor start line
	 * @param cursor_end Cursor end line
	 */
	void display_enable_cursor(uint8_t cursor_start, uint8_t cursor_end);

	/**
	 * @brief Disable cursor display
	 */
	void display_disable_cursor(void);

	/**
	 * @brief Update cursor position
	 * @param x X coordinate (column)
	 * @param y Y coordinate (row)
	 */
	void display_update_cursor(int x, int y);

	/**
	 * @brief Get current cursor position
	 * @param x Pointer to store X coordinate
	 * @param y Pointer to store Y coordinate
	 */
	void display_get_cursor(int* x, int* y);

	/**
	 * @brief Get display dimensions in characters
	 * @param width Pointer to store width in characters
	 * @param height Pointer to store height in characters (lines)
	 */
	void display_get_dimensions(int* width, int* height);

	/**
	 * @brief Set the font for framebuffer mode
	 * @param font Pointer to apollo_font to use
	 */
	void display_set_font(const apollo_font* font);

	/**
	 * @brief Set the font scaling for framebuffer mode
	 * @param x_scale X axis scaling factor (1 = normal size)
	 * @param y_scale Y axis scaling factor (1 = normal size)
	 */
	void display_set_font_scale(uint8_t x_scale, uint8_t y_scale);

	/**
	 * @brief Get the current font instance (framebuffer mode only)
	 * @return Pointer to the current font instance, or NULL if not set
	 */
	const apollo_font_instance* display_get_font_instance(void);

#ifdef __is_kernel_
/**
 * @brief Display a kernel panic screen (pink/red screen)
 * @param error Error message to display
 */
	void display_panic(const char* error);

	/**
	 * @brief Display a kernel panic screen with multiple lines
	 * @param errors Array of error message strings
	 * @param length Number of strings in the array
	 */
	void display_panic_array(const char** errors, uint8_t length);
#endif // __is_kernel_

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_H