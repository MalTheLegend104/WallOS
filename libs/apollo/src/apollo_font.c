/**
 * @file apollo_font.c
 * @brief Handles all the font printing related to Apollo
 * @version v0.2
 * @date 1/6/2026
 */
#include <apollo.h>

#define GET_BIT(byte, bit) ((byte & (1 << bit)) >> bit)

extern int printf_serial(const char* restrict format, ...);

void apollo_print_char(framebuffer_t* fb, const apollo_font_instance* font_inst, int c, coordinate_pair p, apollo_font_color_t color) {
	if (fb == NULL || fb->info == NULL || font_inst == NULL || font_inst->font == NULL) return;

	const apollo_font* font = font_inst->font;
	uint8_t x_scale = font_inst->x_scaling;
	uint8_t y_scale = font_inst->y_scaling;

	if (c > font->max_char) { printf_serial("above max char?\r\n"); return; }
	if (c < 0) { printf_serial("negative\r\n"); return; }

	apollo_color_t foreground = color.foreground;
	if (foreground.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &foreground);
	}

	apollo_color_t background = color.background;
	if (background.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &background);
	}

	uint32_t scaled_width = font->font_width * x_scale;
	uint32_t scaled_height = font->font_height * y_scale;

	if (scaled_width > fb->info->width) return;
	if (scaled_height > fb->info->height) return;

	// We aren't writing across the edge of the screen
	if (p.x + scaled_width > fb->info->width) return;
	// Writing past height is a buffer overflow
	if (p.y + scaled_height > fb->info->height) return;

	uint8_t* pixel = apollo_get_point(fb, p);
	bool not_exact = false;
	uint8_t ignored = 0;
	uint8_t width_bytes;
	int row_width;

	if (font->font_width % 8 != 0) {
		// We assume that any bitmap font that's not at a multiple of 8 will be CENTERED in the nearest upper multiple of 8
		// A 20 bit font would be in a 24 bit spot, it just wouldn't use the 2 leftmost and 2 rightmost bits.
		// A 10 bit font would be in a 16 bit spot, it wouldn't use the left most 3 or the right most 3
		// Font widths MUST be a multiple of 2
		not_exact = true;
		ignored = (8 - (font->font_width % 8)) / 2;
		width_bytes = (font->font_width / 8) + 1;
		// I do not understand why I have to subtract 1 for it to work.
		// It just does, and I ain't going to question it.
		// It should work on any width font (unless it's divisible by 8).
		// Tested on 2-14 bit fonts. In theory, it works on higher bit fonts as well.
		row_width = (font->font_width - 1) * fb->info->pixel_width * x_scale;
	} else {
		width_bytes = font->font_width / 8;
		row_width = font->font_width * fb->info->pixel_width * x_scale;
	}

	// Bitmap fonts are directly through.
	// The offset to the char we're printing is at the offset of c * font_height * font_width(in bytes, not bits)
	// We're going to find the position, then check each bit
	uint8_t* char_start = font->bitmap_base + width_bytes * c * font->font_height;

	for (int h = 0; h < font->font_height; h++) {
		for (int sy = 0; sy < y_scale; sy++) {
			uint8_t* row_pixel = pixel;
			for (int i = width_bytes - 1; i >= 0; i--) {
				for (int w = 8; w > 0; w--) {
					if (not_exact && i == width_bytes - 1) { if ((w - 1) + ignored > 7) continue; }
					if (not_exact && i == 0) { if ((w - 1) < ignored) continue; }

					apollo_color_t pixel_color;
					if (GET_BIT(char_start[(h * width_bytes) + i], (w - 1)) == 1) {
						pixel_color = foreground;
					} else {
						pixel_color = background;
					}

					for (int sx = 0; sx < x_scale; sx++) {
						apollo_set_pixel(fb->info, row_pixel, pixel_color);
						row_pixel += fb->info->pixel_width;
					}
				}
			}
			pixel += fb->info->pitch;
		}
	}
}

#include <stdbool.h>

coordinate_pair apollo_print_string(framebuffer_t* fb, const apollo_font_instance* font_inst, const char* s, coordinate_pair p, apollo_font_color_t color, bool wrap, bool newline) {
	if (fb == NULL || fb->info == NULL || font_inst == NULL || font_inst->font == NULL) return p;

	apollo_font* font = font_inst->font;
	uint8_t x_scale = font_inst->x_scaling;
	uint8_t y_scale = font_inst->y_scaling;

	uint32_t scaled_width = font->font_width * x_scale;
	uint32_t scaled_height = font->font_height * y_scale;

	// We cant print out characters larger than the screen
	if (scaled_width > fb->info->width) return p;
	if (scaled_height > fb->info->height) return p;

	apollo_color_t foreground = color.foreground;
	if (foreground.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &foreground);
	}

	apollo_color_t background = color.background;
	if (background.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &background);
	}

	coordinate_pair pair = { p.x, p.y };
	bool not_exact = false;

	if (font->font_width % 8 != 0) {
		not_exact = true;
	}

	int i = 0;
	unsigned char current_char = s[0];
	while (current_char != '\0') {
		// Deal with new line characters
		if (current_char == '\n' && newline) {
			pair.x = p.x;
			pair.y += scaled_height;
			i++;
			current_char = s[i];
			continue;
		}

		// wrap the text after it is done
		if (pair.x + scaled_width > fb->info->width) {
			if (!wrap) return pair;
			pair.x = p.x;
			pair.y += scaled_height;
		}

		apollo_print_char(fb, font_inst, current_char, pair, (apollo_font_color_t) { foreground, background });

		i++;
		current_char = s[i];
		if (not_exact) pair.x += (font->font_width - 1) * x_scale;
		else pair.x += scaled_width;
	}

	return pair;
}

int apollo_get_chars_per_line(framebuffer_t* fb, apollo_font_instance* font_inst) {
	if (fb == NULL || fb->info == NULL || font_inst == NULL || font_inst->font == NULL) return 0;

	uint32_t scaled_width = font_inst->font->font_width * font_inst->x_scaling;
	return fb->info->width / scaled_width;
}

int apollo_get_max_lines(framebuffer_t* fb, apollo_font_instance* font_inst) {
	if (fb == NULL || fb->info == NULL || font_inst == NULL || font_inst->font == NULL) return 0;

	int height = fb->info->height; // Height is in pixels. Each bit in a bitmap font is a pixel.
	uint32_t scaled_height = font_inst->font->font_height * font_inst->y_scaling;

	return (height / scaled_height);
}