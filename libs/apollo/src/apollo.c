/**
 * @file apollo.c
 * @brief Rectangles and lines implementation in WallOS Apollo
 * @version v0.1
 * @date 8/29/2024
 */

#include <apollo.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

void apollo_draw_line(framebuffer_t* fb, coordinate_pair p1, coordinate_pair p2, apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;

	apollo_color_t current_color = color;

	// Ensure the color matches the framebuffer's format
	if (color.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &current_color);
	}

	if (p1.x == p2.x) {
		// Verticals. We just increment by the pitch.
		const int y_start = (p1.y < p2.y) ? p1.y : p2.y;
		const int y_end = (p1.y > p2.y) ? p1.y : p2.y;

		uint8_t* pixel = apollo_get_point(fb, (coordinate_pair) { p1.x, y_start });

		for (int y = y_start; y < y_end; y++) {
			apollo_set_pixel(fb->info, pixel, current_color);
			pixel += fb->info->pitch;
		}
	} else if (p1.y == p2.y) {
		// Horizontals. We just increment to the right.
		const int x_start = (p1.x < p2.x) ? p1.x : p2.x;
		const int x_end = (p1.x > p2.x) ? p1.x : p2.x;

		uint8_t* pixel = apollo_get_point(fb, (coordinate_pair) { x_start, p1.y });

		for (int x = x_start; x <= x_end; x++) {
			// apollo_set_point(fb,(coordinate_pair) {x, p1.y}, current_color);
			apollo_set_pixel(fb->info, pixel, current_color);
			pixel += fb->info->pixel_width;
		}
	} else {
		// Diagonals are annoying
		// We are going to use Bresenham's line algorithm for diagonal lines
		// It avoids floating point math, making it quicker than other possible implementations.
		// This is still significantly slower than the other line types, but we cant really avoid it.
		const int dx = abs(p2.x - p1.x);
		const int dy = abs(p2.y - p1.y);
		const int sx = (p1.x < p2.x) ? 1 : -1;
		const int sy = (p1.y < p2.y) ? 1 : -1;
		int err = dx - dy;

		while (true) {
			// Set the current pixel
			coordinate_pair point = { p1.x, p1.y };
			apollo_set_point(fb, point, current_color);

			// Check if the line is complete
			if (p1.x == p2.x && p1.y == p2.y) break;

			// Calculate the next pixel position
			int e2 = 2 * err;
			if (e2 > -dy) {
				err -= dy;
				p1.x += sx;
			}
			if (e2 < dx) {
				err += dx;
				p1.y += sy;
			}
		}
	}
}

void apollo_draw_rect(framebuffer_t* fb, coordinate_pair pair, pair_type type, int width, int height, apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;

	apollo_color_t current_color = color;
	if (current_color.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &current_color);
	}

	int start_x = pair.x;
	int start_y = pair.y;

	// We're going to convert everything to top left, it's just much easier that way.
	switch (type) {
		case PAIR_CENTER:
			start_x = pair.x - (width / 2);
			start_y = pair.y - (height / 2);
			break;
		case PAIR_TOP_RIGHT:
			start_x = pair.x - width;
			start_y = pair.y;
			break;
		case PAIR_BOTTOM_LEFT:
			start_x = pair.x;
			start_y = pair.y - height;
			break;
		case PAIR_BOTTOM_RIGHT:
			start_x = pair.x - width;
			start_y = pair.y - height;
			break;
		// Top left is already where we want it.
		default: break;
	}
	int end_x = start_x + width;
	int end_y = start_y + height;
	if (type == PAIR_CENTER) {
		// We need to make sure we actually do it the width
		// We truncated the result above if it's odd
		if (width % 2 != 0) end_x++;
		if (height % 2 != 0) end_y++;
	}
	// Top
	apollo_draw_line(fb, (coordinate_pair) { start_x, start_y }, (coordinate_pair) { end_x, start_y }, current_color);

	// Left
	apollo_draw_line(fb, (coordinate_pair) { start_x, start_y }, (coordinate_pair) { start_x, end_y }, current_color);

	// Bottom
	apollo_draw_line(fb, (coordinate_pair) { start_x, end_y }, (coordinate_pair) { end_x, end_y }, current_color);

	// Right
	apollo_draw_line(fb, (coordinate_pair) { end_x, start_y }, (coordinate_pair) { end_x, end_y }, current_color);
}

void apollo_outline_rect(framebuffer_t* fb, coordinate_pair pair, pair_type type, int width, int height, int border_width, apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;

	apollo_color_t current_color = color;
	if (current_color.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &current_color);
	}

	int start_x = pair.x;
	int start_y = pair.y;

	// We're going to convert everything to top left, it's just much easier that way.
	switch (type) {
		case PAIR_CENTER:
			start_x = pair.x - (width / 2);
			start_y = pair.y - (height / 2);
			break;
		case PAIR_TOP_RIGHT:
			start_x = pair.x - width;
			start_y = pair.y;
			break;
		case PAIR_BOTTOM_LEFT:
			start_x = pair.x;
			start_y = pair.y - height;
			break;
		case PAIR_BOTTOM_RIGHT:
			start_x = pair.x - width;
			start_y = pair.y - height;
			break;
		// Top left is already where we want it.
		default: break;
	}
	int end_x = start_x + width;
	int end_y = start_y + height;

	if (type == PAIR_CENTER) {
		// We need to make sure we actually do it the width
		// We truncated the result above if it's odd
		if (width % 2 != 0) end_x++;
		if (height % 2 != 0) end_y++;
	}

	for (int i = 0; i < border_width; i++) {
		// Top
		apollo_draw_line(fb, (coordinate_pair) { start_x - i, start_y - i }, (coordinate_pair) { end_x + i, start_y - i }, current_color);

		// Left
		apollo_draw_line(fb, (coordinate_pair) { start_x - i, start_y - i }, (coordinate_pair) { start_x - i, end_y + i }, current_color);

		// Bottom
		apollo_draw_line(fb, (coordinate_pair) { start_x - i, end_y + i }, (coordinate_pair) { end_x + i, end_y + i }, current_color);

		// Right
		apollo_draw_line(fb, (coordinate_pair) { end_x + i, start_y - i }, (coordinate_pair) { end_x + i, end_y + i }, current_color);
	}
}

void apollo_fill_rect(framebuffer_t* fb, coordinate_pair pair, pair_type type, int width, int height, apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;

	apollo_color_t current_color = color;
	if (current_color.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &current_color);
	}

	int start_x = pair.x;
	int start_y = pair.y;

	// We're going to convert everything to top left, it's just much easier that way.
	switch (type) {
		case PAIR_CENTER:
			start_x = pair.x - (width / 2);
			start_y = pair.y - (height / 2);
			break;
		case PAIR_TOP_RIGHT:
			start_x = pair.x - width;
			start_y = pair.y;
			break;
		case PAIR_BOTTOM_LEFT:
			start_x = pair.x;
			start_y = pair.y - height;
			break;
		case PAIR_BOTTOM_RIGHT:
			start_x = pair.x - width;
			start_y = pair.y - height;
			break;
		// Top left is already where we want it.
		default: break;
	}

	if (type == PAIR_CENTER) {
		// We need to make sure we actually do it the correct width
		// We truncated the result above if it's odd
		if (width % 2 != 0) width++;
		if (height % 2 != 0) height++;
	}

	// We just fill the rectangle row by row.
	// This is way more efficient than repeated calls to draw_line
	uint8_t* pixel = apollo_get_point(fb, (coordinate_pair) { start_x, start_y });
	const int row_width = width * fb->info->pixel_width;

	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			apollo_set_pixel(fb->info, pixel, current_color);
			pixel += fb->info->pixel_width;
		}
		pixel += fb->info->pitch - row_width;
	}
}