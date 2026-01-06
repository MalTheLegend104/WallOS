#include <klibc/display.h>
#include <klibc/kprint.h>
#include <apollo.h>
#include <string.h>

#include <fonts/apollo_12x18.h>

// ------------------------------------------------------------------------------------------------
// Internal State
// ------------------------------------------------------------------------------------------------
static display_mode_t g_display_mode = DISPLAY_MODE_VGA_TEXT;
static display_color_t g_current_fg = DISPLAY_DEFAULT_FG;
static display_color_t g_current_bg = DISPLAY_DEFAULT_BG;

// Framebuffer mode state
static framebuffer_t g_fb;
static framebuffer_info_t g_fb_info;
static apollo_font_instance g_font_instance = { NULL, 1, 1 };
static int g_fb_cursor_x = 0;
static int g_fb_cursor_y = 0;

// ------------------------------------------------------------------------------------------------
// Color Conversion
// ------------------------------------------------------------------------------------------------
static apollo_color_t display_color_to_apollo(display_color_t color) {
	apollo_color_t result = { .alpha = 255, .red = 0, .green = 0, .blue = 0, .type = APOLLO_PIXEL_TYPE_ARGB8888 };

	switch (color) {
		case DISPLAY_COLOR_BLACK:       result.red = 0;   result.green = 0;   result.blue = 0;   break;
		case DISPLAY_COLOR_BLUE:        result.red = 0;   result.green = 0;   result.blue = 170; break;
		case DISPLAY_COLOR_GREEN:       result.red = 0;   result.green = 170; result.blue = 0;   break;
		case DISPLAY_COLOR_CYAN:        result.red = 0;   result.green = 170; result.blue = 170; break;
		case DISPLAY_COLOR_RED:         result.red = 170; result.green = 0;   result.blue = 0;   break;
		case DISPLAY_COLOR_PURPLE:      result.red = 170; result.green = 0;   result.blue = 170; break;
		case DISPLAY_COLOR_BROWN:       result.red = 170; result.green = 85;  result.blue = 0;   break;
		case DISPLAY_COLOR_LIGHT_GREY:  result.red = 170; result.green = 170; result.blue = 170; break;
		case DISPLAY_COLOR_DARK_GREY:   result.red = 85;  result.green = 85;  result.blue = 85;  break;
		case DISPLAY_COLOR_LIGHT_BLUE:  result.red = 85;  result.green = 85;  result.blue = 255; break;
		case DISPLAY_COLOR_LIGHT_GREEN: result.red = 85;  result.green = 255; result.blue = 85;  break;
		case DISPLAY_COLOR_LIGHT_CYAN:  result.red = 85;  result.green = 255; result.blue = 255; break;
		case DISPLAY_COLOR_LIGHT_RED:   result.red = 255; result.green = 85;  result.blue = 85;  break;
		case DISPLAY_COLOR_PINK:        result.red = 255; result.green = 85;  result.blue = 255; break;
		case DISPLAY_COLOR_YELLOW:      result.red = 255; result.green = 255; result.blue = 85;  break;
		case DISPLAY_COLOR_WHITE:       result.red = 255; result.green = 255; result.blue = 255; break;
		default:                        result.red = 170; result.green = 170; result.blue = 170; break;
	}

	return result;
}

// ------------------------------------------------------------------------------------------------
// Framebuffer Mode Helpers
// ------------------------------------------------------------------------------------------------
static void fb_scroll(void) {
	if (!g_font_instance.font) return;

	int font_height = g_font_instance.font->font_height * g_font_instance.y_scaling;
	size_t pitch = g_fb_info.pitch;

	// Move all lines up
	memmove(g_fb.buffer,
		g_fb.buffer + (pitch * font_height),
		pitch * (g_fb_info.height - font_height));

// Clear bottom line
	apollo_color_t bg = display_color_to_apollo(g_current_bg);
	coordinate_pair bottom_left = { 0, g_fb_info.height - font_height };
	apollo_fill_rect(&g_fb, bottom_left, PAIR_TOP_LEFT,
		g_fb_info.width, font_height, bg);
}

static void fb_putc_internal(char c) {
	if (!g_font_instance.font) return;

	int char_width = g_font_instance.font->font_width * g_font_instance.x_scaling;
	int char_height = g_font_instance.font->font_height * g_font_instance.y_scaling;
	int chars_per_line = g_fb_info.width / char_width;
	int max_lines = g_fb_info.height / char_height;

	apollo_font_color_t colors;
	colors.foreground = display_color_to_apollo(g_current_fg);
	colors.background = display_color_to_apollo(g_current_bg);

	if (c == '\n') {
		g_fb_cursor_x = 0;
		g_fb_cursor_y++;
		if (g_fb_cursor_y >= max_lines) {
			fb_scroll();
			g_fb_cursor_y = max_lines - 1;
		}
		return;
	}

	if (c == '\r') {
		g_fb_cursor_x = 0;
		return;
	}

	if (c == '\b') {
		if (g_fb_cursor_x > 0) {
			g_fb_cursor_x--;
			coordinate_pair pos = {
				g_fb_cursor_x * char_width,
				g_fb_cursor_y * char_height
			};
			apollo_fill_rect(&g_fb, pos, PAIR_TOP_LEFT,
				char_width, char_height,
				colors.background);
		}
		return;
	}

	coordinate_pair pos = {
		g_fb_cursor_x * char_width,
		g_fb_cursor_y * char_height
	};
	apollo_print_char(&g_fb, &g_font_instance, c, pos, colors);

	g_fb_cursor_x++;
	if (g_fb_cursor_x >= chars_per_line) {
		g_fb_cursor_x = 0;
		g_fb_cursor_y++;
		if (g_fb_cursor_y >= max_lines) {
			fb_scroll();
			g_fb_cursor_y = max_lines - 1;
		}
	}
}

// ------------------------------------------------------------------------------------------------
// Public API Implementation
// ------------------------------------------------------------------------------------------------

bool display_init(display_mode_t mode) {
	g_display_mode = mode;

	if (mode == DISPLAY_MODE_VGA_TEXT) {
		initScreen();
		return true;
	} else if (mode == DISPLAY_MODE_FRAMEBUFFER) {
		// Get framebuffer info from Apollo
		apollo_get_info(&g_fb_info);
		g_fb.info = &g_fb_info;
		// Note: You'll need to allocate or get the buffer pointer
		// g_fb.buffer = your_framebuffer_pointer;

		g_fb_cursor_x = 0;
		g_fb_cursor_y = 0;

		g_font_instance.font = &apollo_12x18;
		g_font_instance.x_scaling = 1;
		g_font_instance.y_scaling = 1;

		if (g_fb.buffer && g_font_instance.font) {
			apollo_color_t bg = display_color_to_apollo(DISPLAY_DEFAULT_BG);
			apollo_fill_buffer(&g_fb, bg);
			return true;
		}
		return false;
	}

	return false;
}

display_mode_t display_get_mode(void) {
	return g_display_mode;
}

bool display_switch_mode(display_mode_t mode) {
	return display_init(mode);
}

void display_clear(void) {
	if (g_display_mode == DISPLAY_MODE_VGA_TEXT) {
		clearVGABuf();
	} else {
		apollo_color_t bg = display_color_to_apollo(g_current_bg);
		apollo_fill_buffer(&g_fb, bg);
		g_fb_cursor_x = 0;
		g_fb_cursor_y = 0;
	}
}

void display_clear_row(void) {
	if (g_display_mode == DISPLAY_MODE_VGA_TEXT) {
		clear_current_row();
	} else {
		if (!g_font_instance.font) return;
		apollo_color_t bg = display_color_to_apollo(g_current_bg);
		int char_height = g_font_instance.font->font_height * g_font_instance.y_scaling;
		coordinate_pair pos = { 0, g_fb_cursor_y * char_height };
		apollo_fill_rect(&g_fb, pos, PAIR_TOP_LEFT,
			g_fb_info.width, char_height, bg);
		g_fb_cursor_x = 0;
	}
}

void display_set_colors(display_color_t fg, display_color_t bg) {
	g_current_fg = fg;
	g_current_bg = bg;

	if (g_display_mode == DISPLAY_MODE_VGA_TEXT) {
		set_colors(fg, bg);
	}
	// Framebuffer mode stores colors and applies them during rendering
}

void display_set_colors_default(void) {
	display_set_colors(DISPLAY_DEFAULT_FG, DISPLAY_DEFAULT_BG);
}

void display_putc(char c) {
	if (g_display_mode == DISPLAY_MODE_VGA_TEXT) {
		putc_vga(c);
	} else {
		fb_putc_internal(c);
	}
}

void display_putc_unfiltered(char c) {
	if (g_display_mode == DISPLAY_MODE_VGA_TEXT) {
		putc_vga_unfiltered(c);
	} else {
		fb_putc_internal(c);
	}
}

void display_puts(const char* str) {
	if (g_display_mode == DISPLAY_MODE_VGA_TEXT) {
		puts_vga(str);
	} else {
		for (const char* p = str; *p; p++) {
			fb_putc_internal(*p);
		}
	}
}

void display_puts_color(const char* str, display_color_t fg, display_color_t bg) {
	if (g_display_mode == DISPLAY_MODE_VGA_TEXT) {
		puts_vga_color(str, fg, bg);
	} else {
		display_color_t old_fg = g_current_fg;
		display_color_t old_bg = g_current_bg;
		display_set_colors(fg, bg);
		display_puts(str);
		display_set_colors(old_fg, old_bg);
	}
}

void display_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
	if (g_display_mode == DISPLAY_MODE_VGA_TEXT) {
		enable_cursor(cursor_start, cursor_end);
	}
	// Framebuffer mode: Could implement a software cursor if needed
}

void display_disable_cursor(void) {
	if (g_display_mode == DISPLAY_MODE_VGA_TEXT) {
		disable_cursor();
	}
}

void display_update_cursor(int x, int y) {
	if (g_display_mode == DISPLAY_MODE_VGA_TEXT) {
		update_cursor(x, y);
	} else {
		g_fb_cursor_x = x;
		g_fb_cursor_y = y;
	}
}

void display_get_cursor(int* x, int* y) {
	if (g_display_mode == DISPLAY_MODE_VGA_TEXT) {
		// You'll need to add this to kprint.h if it doesn't exist
		// For now, stub it
		if (x) *x = 0;
		if (y) *y = 0;
	} else {
		if (x) *x = g_fb_cursor_x;
		if (y) *y = g_fb_cursor_y;
	}
}

void display_get_dimensions(int* width, int* height) {
	if (g_display_mode == DISPLAY_MODE_VGA_TEXT) {
		if (width) *width = 80;
		if (height) *height = 25;
	} else {
		if (g_font_instance.font) {
			int char_width = g_font_instance.font->font_width * g_font_instance.x_scaling;
			int char_height = g_font_instance.font->font_height * g_font_instance.y_scaling;
			if (width) *width = g_fb_info.width / char_width;
			if (height) *height = g_fb_info.height / char_height;
		} else {
			if (width) *width = 0;
			if (height) *height = 0;
		}
	}
}

void display_set_font(const apollo_font* font) {
	g_font_instance.font = font;
}

void display_set_font_scale(uint8_t x_scale, uint8_t y_scale) {
	if (x_scale == 0) x_scale = 1;
	if (y_scale == 0) y_scale = 1;
	g_font_instance.x_scaling = x_scale;
	g_font_instance.y_scaling = y_scale;
}

const apollo_font_instance* display_get_font_instance(void) {
	if (g_display_mode == DISPLAY_MODE_FRAMEBUFFER) {
		return &g_font_instance;
	}
	return NULL;
}

#ifdef __is_kernel_
void display_panic(const char* error) {
	if (g_display_mode == DISPLAY_MODE_VGA_TEXT) {
		pink_screen(error);
	} else {
		// Implement framebuffer panic screen
		apollo_color_t pink = display_color_to_apollo(DISPLAY_COLOR_PINK);
		apollo_fill_buffer(&g_fb, pink);

		g_fb_cursor_x = 0;
		g_fb_cursor_y = 0;
		display_set_colors(DISPLAY_COLOR_WHITE, DISPLAY_COLOR_PINK);
		display_puts(error);
	}
}

void display_panic_array(const char** errors, uint8_t length) {
	if (g_display_mode == DISPLAY_MODE_VGA_TEXT) {
		pink_screen_sa(errors, length);
	} else {
		apollo_color_t pink = display_color_to_apollo(DISPLAY_COLOR_PINK);
		apollo_fill_buffer(&g_fb, pink);

		g_fb_cursor_x = 0;
		g_fb_cursor_y = 0;
		display_set_colors(DISPLAY_COLOR_WHITE, DISPLAY_COLOR_PINK);

		for (uint8_t i = 0; i < length; i++) {
			display_puts(errors[i]);
			display_putc('\n');
		}
	}
}
#endif // __is_kernel_