/**
 * @file apollo_font.c
 * @brief Handles all the font printing related to Apollo
 * @version v0.2
 * @date 1/6/2026
 */
#include <apollo.h>

#define GET_BIT(byte, bit) ((byte & (1 << bit)) >> bit)
// Writes one glyph row (already expanded into 'bits') into the framebuffer.
// 'bits' holds the row's bytes as a little-endian integer, so the leftmost pixel is bit 'top'  and the rightmost is bit 'bot'.
static inline void apollo_glyph_row(uint8_t* q, uint32_t bits, int top, int bot, int x_scale, uint32_t fg, uint32_t bg, unsigned bpp) {
	if (bpp == 4) {
		uint32_t* d = (uint32_t*) q;
		for (int b = top; b >= bot; b--) {
			const uint32_t v = ((bits >> b) & 1u) ? fg : bg;
			for (int sx = 0; sx < x_scale; sx++) *d++ = v;
		}
	} else if (bpp == 2) {
		uint16_t* d = (uint16_t*) q;
		for (int b = top; b >= bot; b--) {
			const uint16_t v = (uint16_t) (((bits >> b) & 1u) ? fg : bg);
			for (int sx = 0; sx < x_scale; sx++) *d++ = v;
		}
	} else if (bpp == 1) {
		for (int b = top; b >= bot; b--) {
			const uint8_t v = (uint8_t) (((bits >> b) & 1u) ? fg : bg);
			for (int sx = 0; sx < x_scale; sx++) *q++ = v;
		}
	} else { // 3 bytes per pixel
		for (int b = top; b >= bot; b--) {
			const uint32_t v = ((bits >> b) & 1u) ? fg : bg;
			for (int sx = 0; sx < x_scale; sx++) {
				q[0] = (uint8_t) v;
				q[1] = (uint8_t) (v >> 8);
				q[2] = (uint8_t) (v >> 16);
				q += 3;
			}
		}
	}
}

void apollo_print_char(framebuffer_t* fb, const apollo_font_instance* font_inst, int c, coordinate_pair p, apollo_font_color_t color) {
	if (fb == NULL || fb->info == NULL || fb->buffer == NULL) return;
	if (font_inst == NULL || font_inst->font == NULL) return;

	const apollo_font* font = font_inst->font;
	const framebuffer_info_t* info = fb->info;

	const int x_scale = font_inst->x_scaling;
	const int y_scale = font_inst->y_scaling;
	if (x_scale == 0 || y_scale == 0) return;
	if (c < 0 || c > (int) font->max_char) return;

	const int scaled_width = font->font_width * x_scale;
	const int scaled_height = font->font_height * y_scale;

	// Don't draw anything that isn't entirely on screen
	if (p.x < 0 || p.y < 0) return;
	if (p.x + scaled_width > info->width) return;
	if (p.y + scaled_height > info->height) return;

	// Glyph rows are width_bytes wide.
	// Fonts whose width isn't a multiple of 8 are centered in the next multiple of 8, so 'ignored' unused bits sit on each side (width must be even)
	const int width_bytes = (font->font_width + 7) / 8;
	if (width_bytes > 4) return; // a row must fit in a uint32_t
	const int ignored = (font->font_width % 8 != 0) ? (8 - (font->font_width % 8)) / 2 : 0;
	const int top = 8 * width_bytes - 1 - ignored; // leftmost pixel's bit
	const int bot = ignored;                       // rightmost pixel's bit

	const unsigned bpp = info->pixel_width;
	const uint32_t fg = apollo_pack_color(info, color.foreground);
	const uint32_t bg = apollo_pack_color(info, color.background);

	uint8_t* pixel = fb->buffer + (size_t) p.y * info->pitch + (size_t) p.x * bpp;
	const uint8_t* char_start = font->bitmap_base + (size_t) width_bytes * c * font->font_height;

	for (int h = 0; h < font->font_height; h++) {
		// The last byte of the row is the leftmost 8 pixels, so treating the row as a little-endian integer puts the leftmost pixel at the top bit
		uint32_t bits = 0;
		for (int i = 0; i < width_bytes; i++) {
			bits |= (uint32_t) char_start[h * width_bytes + i] << (8 * i);
		}

		// Rebuild the row for each vertical repeat instead of copying the first one down
		// This keeps it write-only, which matters when the target is write-combined VRAM
		for (int sy = 0; sy < y_scale; sy++) {
			apollo_glyph_row(pixel, bits, top, bot, x_scale, fg, bg, bpp);
			pixel += info->pitch;
		}
	}
}

#include <stdbool.h>

coordinate_pair apollo_print_string(framebuffer_t* fb, const apollo_font_instance* font_inst, const char* s, coordinate_pair p, apollo_font_color_t color, bool wrap, bool newline) {
	if (fb == NULL || fb->info == NULL || font_inst == NULL || font_inst->font == NULL) return p;

	const apollo_font* font = font_inst->font;
	uint8_t x_scale = font_inst->x_scaling;
	uint8_t y_scale = font_inst->y_scaling;

	int32_t scaled_width = font->font_width * x_scale;
	int32_t scaled_height = font->font_height * y_scale;

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

	coordinate_pair pair = {p.x, p.y};
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

		apollo_print_char(fb, font_inst, current_char, pair, (apollo_font_color_t) {foreground, background});

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