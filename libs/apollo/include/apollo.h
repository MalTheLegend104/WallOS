/**
 * @file apollo.h
 * @brief Main include file for the WallOS Apollo GFX Framework
 * @version v0.1
 * @date 8/28/2024
 */
#ifndef WALLOS_APOLLO_H
#define WALLOS_APOLLO_H

#include <stdint.h>
#include <stdbool.h>

// The user of the library has to maintain their own buffer
// The entire framework is just dedicated to helper functions and kernel API
// There will be a dedicated "application" framework built off of this one that handles creating windows
// This one will be used internally to create the window manager and handle graphics in general.

#ifdef __cplusplus
extern "C" {
#endif
	/* The following comments are brief information about aspects of the library. */
	/* Color: Most systems only have 32-bit color. We will only support 32-bit (or lower) color. */
	/* Anti-Aliasing: None, unless it's built into the monitor itself. Diagonals/circles are rough approximations. */

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Essential Structures
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	typedef enum {
		APOLLO_PIXEL_TYPE_UNKNOWN = 0,  	// Unknown pixel type
		APOLLO_PIXEL_TYPE_ARGB8888 = 1,	// 32-bit ARGB 	(8 bits per channel, including alpha)
		APOLLO_PIXEL_TYPE_RGB32 = 2,    // 32-bit RGB 	(8 bits per channel, no alpha) Essentially RGBA in memory (lower bits are reserved instead of alpha)
		APOLLO_PIXEL_TYPE_RGBA32 = 3,    // 32-bit RGBA 	(8 bits per channel, including alpha)
		APOLLO_PIXEL_TYPE_BGR32 = 4,    // 32-bit BGR 	(8 bits per channel, no alpha) Essentially BGRA in memory (lower bits are reserved instead of alpha)
		APOLLO_PIXEL_TYPE_BGRA32 = 5,    // 32-bit BGRA 	(8 bits per channel, including alpha)
		APOLLO_PIXEL_TYPE_RGB24 = 6,    // 24-bit RGB 	(8 bits per channel, no alpha)
		APOLLO_PIXEL_TYPE_BGR24 = 7,    // 24-bit BGR 	(8 bits per channel, no alpha)
		APOLLO_PIXEL_TYPE_RGB16_565 = 8,    // 16-bit RGB 	(5 bits red, 6 bits green, 5 bits blue)
		APOLLO_PIXEL_TYPE_RGB16_555 = 9,    // 16-bit RGB 	(5 bits red, 5 bits green, 5 bits blue, 1 unused bit)
		APOLLO_PIXEL_TYPE_BGR16_565 = 10,   // 16-bit BGR 	(5 bits blue, 6 bits green, 5 bits red)
		APOLLO_PIXEL_TYPE_BGR16_555 = 11,   // 16-bit BGR  	(5 bits blue, 5 bits green, 5 bits red, 1 unused bit)
		APOLLO_PIXEL_TYPE_ARGB4444 = 12,	// 16-bit ARGB 	(4 bits per channel, including alpha)
		APOLLO_PIXEL_TYPE_RGBA4444 = 13,	// 16-bit RGBA 	(4 bits per channel, including alpha)
		APOLLO_PIXEL_TYPE_ABGR4444 = 14,	// 16-bit RGBA 	(4 bits per channel, including alpha)
		APOLLO_PIXEL_TYPE_BGRA4444 = 15,	// 16-bit RGBA 	(4 bits per channel, including alpha)
		APOLLO_PIXEL_TYPE_RGB332 = 16	// 8-bit  RGB	(3 bits red, 3 bits green, 2 bits blue, no alpha)
	} apollo_pixel_type;

	typedef struct {
		int width;
		int height;
		int pitch;
		uint8_t pixel_width;
		apollo_pixel_type type;
	} framebuffer_info_t;

	typedef struct {
		framebuffer_info_t* info;
		uint8_t* buffer;
	} framebuffer_t;

	typedef struct {
		uint8_t alpha;
		uint8_t red;
		uint8_t green;
		uint8_t blue;
		apollo_pixel_type type;
	} apollo_color_t;

	typedef struct {
		apollo_color_t foreground;
		apollo_color_t background;
	} apollo_font_color_t;

	typedef struct {
		int x;
		int y;
	} coordinate_pair;

	typedef enum {
		PAIR_CENTER,
		PAIR_TOP_LEFT,
		PAIR_TOP_RIGHT,
		PAIR_BOTTOM_LEFT,
		PAIR_BOTTOM_RIGHT,
	} pair_type;

	typedef struct {
		uint8_t* bitmap_base; // The base pointer to the array holding the bitmap data.
		uint8_t font_width;   // The width of the font in pixels (a 8x8 font would have 8 height and 8 width)
		uint8_t font_height;  // The height of the font in pixels
		uint32_t max_char; 	  // The maximum character supported by the font.
	} apollo_font;

	typedef struct {
		const apollo_font* font;
		uint8_t x_scaling;
		uint8_t y_scaling;
	} apollo_font_instance;

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Debug Functions - Must define APOLLO_DEBUG
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// TODO: uncomment this
	// #ifdef APOLLO_DEBUG
#include <stdio.h>
	void apollo_print_pixel_type(apollo_pixel_type type);
	void apollo_print_info(framebuffer_info_t* info);
	// #endif

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Kernel Endpoints - These will all be syscalls that allow you to interact with the
	// 					  actual framebuffer.
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	/**
	 * @brief Fills the provided info struct with information about the framebuffer.
	 * @param fb_info User created structure to load the information into.
	 */
	void apollo_get_info(framebuffer_info_t* fb_info);
	/**
	 * @brief Draws the provided buffer onto the screen.
	 * This WILL result in overriding anything already there.
	 *
	 * @param buffer Buffer to be written onto the screen.
	 */
	void apollo_draw_buffer(framebuffer_t* buffer);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Utility Functions
	// Most of these make data manipulation easier.
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	/**
	 * @brief Converts between two color types.
	 * It will be a rough approximation of the color. Converting from 32 to 16 bit will obviously reduce color accuracy.
	 * The lower bit-depths (16 and 24) do not have alpha channels.
	 * @param target_type Target type to convert to.
	 * @param color Current color.
	 */
	void apollo_switch_color(apollo_pixel_type target_type, apollo_color_t* color);

	/**
	 * Sets the pixel at the provided address to the provided color. Converts the color automatically between formats.
	 * @param info The information related to the buffer containing the pixel.
	 * @param pixel Position in the buffer of the pixel.
	 * The address is converted to the correct type (corresponding to the pixel type) before writing.
	 * @param color Color of the pixel. Automatically converted if the format does not match the framebuffer type.
	 */
	void apollo_set_pixel(const framebuffer_info_t* info, uint8_t* pixel, apollo_color_t color);

	/**
	 * @brief Sets the provided point to the provided color.
	 * Only use this function if absolutely necessary.
	 * It is *much* slower to make tones of calls to this to fill a shape as opposed to using other helper functions.
	 * @param fb Framebuffer to write to.
	 * @param p Coordinate pair of the pixel.
	 * @param color Color to set the pixel to.
	 */
	void apollo_set_point(framebuffer_t* fb, coordinate_pair p, apollo_color_t color);

	/**
	 * Gets the pointer to the spot in the framebuffer of the pixel.
	 * @param fb Framebuffer to get the pixel from.
	 * @param pair Coordinate pair of the pixel.
	 * @return Pointer to the pixel in the buffer. NULL if pair is out of the range of the buffer.
	 */
	uint8_t* apollo_get_point(framebuffer_t* fb, coordinate_pair pair);

	/**
	 * @brief Fills the buffer with a single color.
	 * @param fb Framebuffer to write to.
	 * @param color Color to fill the framebuffer with.
	 */
	void apollo_fill_buffer(framebuffer_t* fb, apollo_color_t color);

	/**
	 * Returns the color corresponding to the provided integer and type
	 * This does make the assumption that your color integer is already in the correct format.
	 * You cannot pass through 0xFF as a RGB16_565 and expect it to be pure blue.
	 * @param color Integer containing color value (like 0xFFFFFFFF being ARGB8888 white).
	 * @param type Type of color the integer contains.
	 * @return The corresponding color structure.
	 */
	apollo_color_t apollo_get_color(size_t color, apollo_pixel_type type);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Drawing Functions - These all draw shapes into the framebuffer.
	// Draw Functions: Only outline the shape with a 1px border.
	// Fill Functions: Fill the entire shape.
	// Outline Functions: Draws the outline of the shape, with the specified border width.
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	/**
	 * @brief Draws a line on the framebuffer.
	 * @param fb Framebuffer to write to.
	 * @param p1 Starting (or ending, it doesn't really matter) coordinate pair.
	 * @param p2 Ending coordinate pair.
	 * @param color Color of the line.
	 */
	void apollo_draw_line(framebuffer_t* fb, coordinate_pair p1, coordinate_pair p2, apollo_color_t color);

	/**
	 * @brief Draws a rectangle.
	 * @param fb Framebuffer to write to.
	 * @param pair Coordinate pair at the origin (defined by type).
	 * @param type Type of coordinate pair provided.
	 * @param width Width of the rectangle.
	 * @param height Height of the rectangle.
	 * @param color Color of the rectangle.
	 */
	void apollo_draw_rect(framebuffer_t* fb, coordinate_pair pair, pair_type type, int width, int height, apollo_color_t color);

	/**
	 * @brief Draws and fills a rectangle.
	 * @param fb Framebuffer to write to.
	 * @param pair Coordinate pair at the origin (defined by type).
	 * @param type Type of coordinate pair provided.
	 * @param width Width of the rectangle.
	 * @param height Height of the rectangle.
	 * @param color Color of the rectangle.
	 */
	void apollo_fill_rect(framebuffer_t* fb, coordinate_pair pair, pair_type type, int width, int height, apollo_color_t color);

	/**
	 * @brief Draws and fills a rectangle.
	 * @param fb Framebuffer to write to.
	 * @param pair Coordinate pair at the origin (defined by type).
	 * @param type Type of coordinate pair provided.
	 * @param width Width of the rectangle.
	 * @param height Height of the rectangle.
	 * @param border_width Width of the border in pixels.
	 * @param color Color of the rectangle.
	 */
	void apollo_outline_rect(framebuffer_t* fb, coordinate_pair pair, pair_type type, int width, int height, int border_width, apollo_color_t color);

	/**
	 * Draws a circle.
	 * @param fb Framebuffer to write to.
	 * @param pair Coordinate pair at the center.
	 * @param radius Radius from the center to the border.
	 * @param color Color of the circle.
	 */
	void apollo_draw_circle(framebuffer_t* fb, coordinate_pair pair, int radius, apollo_color_t color);

	/**
	 * Fills a circle.
	 * @param fb Framebuffer to write to.
	 * @param pair Coordinate pair at the center.
	 * @param radius Radius from the center to the border.
	 * @param color Color of the circle.
	 */
	void apollo_fill_circle(framebuffer_t* fb, coordinate_pair pair, int radius, apollo_color_t color);

	/**
	 * Outlines a circle.
	 * This is a slow function, it essentially calls apollo_draw_circle() border_width times. It's also very inaccurate.
	 * It is recommended to call fill circle twice (which isn't much slower), and put things back in the inner circle if needed.
	 * @param fb Framebuffer to write to.
	 * @param pair Coordinate pair at the center.
	 * @param border_width Width of the border in pixels.
	 * @param radius Radius from the center to the border.
	 * @param color Color of the circle.
	 */
	void apollo_outline_circle(framebuffer_t* fb, coordinate_pair pair, int border_width, int radius, apollo_color_t color);

	/**
	 * Draws a triangle.
	 * @param fb Framebuffer to write to.
	 * @param pairs Set of the 3 coordinate pairs of the triangle. If there is not 3 behavior is undefined.
	 * @param color Color of the triangle.
	 */
	void apollo_draw_triangle(framebuffer_t* fb, coordinate_pair* pairs, apollo_color_t color);

	/**
	 * Fills a triangle.
	 * This will not perfectly follow apollo_draw_triangle. It *will* perfectly follow other fill_triangle calls.
	 * It has to use a different formula, as drawing a triangle is essentially just three calls to draw line. This is not possible when filling it.
	 * @param fb Framebuffer to write to.
	 * @param pairs Set of the 3 coordinate pairs of the triangle. If there is not 3 behavior is undefined.
	 * @param color Color of the triangle.
	 */
	void apollo_fill_triangle(framebuffer_t* fb, coordinate_pair* pairs, apollo_color_t color);

	/**
	 * Outlines a triangle.
	 * @param fb Framebuffer to write to.
	 * @param pairs Set of the 3 coordinate pairs of the triangle. If there is not 3 behavior is undefined.
	 * @param border_width Width of the border in pixels.
	 * @param color Color of the triangle.
	 */
	void apollo_outline_triangle(framebuffer_t* fb, coordinate_pair* pairs, int border_width, apollo_color_t color);

	/**
	 * Draws a diamond.
	 * @param fb Framebuffer to write to.
	 * @param pair Coordinate pair at the origin (defined by type).
	 * @param width Width between the two side points.
	 * @param height Height between the top/bottom points.
	 * @param color Color of the diamond.
	 */
	void apollo_draw_diamond(framebuffer_t* fb, coordinate_pair pair, int width, int height, apollo_color_t color);

	/**
	 * Fills a diamond.
	 * @param fb Framebuffer to write to.
	 * @param pair Coordinate pair at the origin (defined by type).
	 * @param width Width between the two side points.
	 * @param height Height between the top/bottom points.
	 * @param color Color of the diamond.
	 */
	void apollo_fill_diamond(framebuffer_t* fb, coordinate_pair pair, int width, int height, apollo_color_t color);

	/**
	 * Outlines a diamond.
	 * @param fb Framebuffer to write to.
	 * @param pair Coordinate pair at the origin (defined by type).
	 * @param width Width between the two side points.
	 * @param height Height between the top/bottom points.
	 * @param border_width Width of the border in pixels.
	 * @param color Color of the diamond.
	 */
	void apollo_outline_diamond(framebuffer_t* fb, coordinate_pair pair, int width, int height, int border_width, apollo_color_t color);

	/**
	 * @brief Draws the polygon in order of the provided pairs. It does NOT check for order itself.
	 * It WILL close the polygon (last coordinate -> starting coordinate) if not already closed.
	 * @param fb Framebuffer to write to.
	 * @param pairs Array of coordinate pairs.
	 * @param pair_count Amount of pairs in the array.
	 * @param color Color of the polygon.
	 */
	void apollo_draw_polygon(framebuffer_t* fb, const coordinate_pair* pairs, int pair_count, apollo_color_t color);

	/**
	 * @brief Fills the polygon in O(nlogn) time, where n is the number of sides.
	 * @param fb Framebuffer to write to.
	 * @param pairs Array of coordinate pairs
	 * @param pair_count Amount of pairs in the array.
	 * @param color Color of the polygon.
	 */
	void apollo_fill_polygon(framebuffer_t* fb, const coordinate_pair* pairs, int pair_count, apollo_color_t color);

	/* There are no outline polygon functions. There are too many calculations for it to be efficient enough to be acceptable to me. */

	/**
	 * @brief Draws a regular n-sided polygon.
	 * This works perfectly for even number sides. Odd number sides may not be oriented the way you want.
	 * @param fb Framebuffer to write to.
	 * @param center Coordinate pair at the center of the polygon.
	 * @param radius Distance from the center to any vertex.
	 * @param sides Number of sides of the polygon (must be 3 or more).
	 * @param color Color of the polygon.
	 */
	void apollo_draw_polygon_n(framebuffer_t* fb, coordinate_pair center, int radius, int sides, apollo_color_t color);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Font Functions - All the functions relating to printing bitmap fonts to the screen.
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/**
	 * Prints the given char at the given position. Position is the *top left* of the character.
	 *
	 * @param fb Framebuffer to write to.
	 * @param font Font to get the data from.
	 * @param c Char to print.
	 * @param p Position to write to.
	 * @param color Color of the character.
	 */
	void apollo_print_char(framebuffer_t* fb, const apollo_font_instance* font, int c, coordinate_pair p, apollo_font_color_t color);

	/**
	 * Prints a NULL terminated string to the framebuffer. Undefined behavior if the string is not null terminated.
	 *
	 * @param fb Framebuffer to write to.
	 * @param font Font to get the data from.
	 * @param s String to print.
	 * @param p Position that the string should start at. It is the *top left* of the first character in the string.
	 * @param color Color of the character
	 * @param wrap Should apollo automatically wrap the string if it would run off the screen.
	 * If true, it will automatically wrap. If false, it will stop writing when it reaches the edge of the screen.
	 * Wrapping will cause the next char to line up directly below original starting character.
	 * @param newline Should the string automatically wrap with newline characters.
	 * Code Page 437 (and others) have characters to display for control characters.
	 * Turning this off allows newlines to print their character, rather than being a control character.
	 * @return The position of the *top right* of the last printed character. This CAN be out of bounds if wrap is false or if the bottom of the screen is reached.
	 */
	coordinate_pair apollo_print_string(framebuffer_t* fb, const apollo_font_instance* font, const char* s, coordinate_pair p, apollo_font_color_t color, bool wrap, bool newline);

	/**
	 * Gets the number of characters per line, given the framebuffer and font.
	 * @param fb Framebuffer to get info from.
	 * @param font Font to get info from.
	 * @return Number of characters per line, given the font and framebuffer.
	 */
	int apollo_get_chars_per_line(framebuffer_t* fb, apollo_font_instance* font);

	/**
	 * Gets the maximum number of lines you can fit in the framebuffer, given the framebuffer and font.
	 * @param fb Framebuffer to get info from.
	 * @param font Font to get info from.
	 * @return Max number of lines given the framebuffer and font.
	 */
	int apollo_get_max_lines(framebuffer_t* fb, apollo_font_instance* font);

#ifdef __cplusplus
}
#endif

#endif // WALLOS_APOLLO_H