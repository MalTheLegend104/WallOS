/**
* @file apollo_triangle.c
 * @brief Triangles implementation in WallOS Apollo
 * @version v0.1
 * @date 8/29/2024
 */
#include <apollo.h>
#include <stdlib.h>

void apollo_draw_triangle(framebuffer_t* fb, coordinate_pair* pairs, apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;

	apollo_color_t current_color = color;
	if (current_color.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &current_color);
	}

	apollo_draw_line(fb, pairs[0], pairs[1], current_color);
	apollo_draw_line(fb, pairs[0], pairs[2], current_color);
	apollo_draw_line(fb, pairs[1], pairs[2], current_color);
}

int interpolate(coordinate_pair p1, coordinate_pair p2, int y) {
	return p1.x + ((y - p1.y) * (p2.x - p1.x)) / (p2.y - p1.y);
}

void swap(coordinate_pair* p1, coordinate_pair* p2) {
	const coordinate_pair temp = *(p1);
	p1->x = p2->x;
	p1->y = p2->y;
	p2->x = temp.x;
	p2->y = temp.y;
}

// This works if there is a vertical on the left and not the right
// It also only works for right triangles
// It's mostly used as a sanity check, and should not be used at all.
// Regular fill triangle works perfectly fine.
void draw_flat_triangle(framebuffer_t* fb, coordinate_pair p1, coordinate_pair p2, coordinate_pair p3, apollo_color_t color) {
	coordinate_pair bottom_left;
	coordinate_pair bottom_right;
	coordinate_pair top;

	if (p1.y == p2.y) {
		if (p1.x < p2.x) {
			bottom_left = p1;
			bottom_right = p2;
		} else {
			bottom_left = p2;
			bottom_right = p1;
		}
		top = p3;
	} else if (p1.y == p3.y) {
		if (p1.x < p3.x) {
			bottom_left = p1;
			bottom_right = p3;
		} else {
			bottom_left = p3;
			bottom_right = p1;
		}
		top = p2;
	} else {
		// p2.y and p3.y are the same
		if (p2.x < p3.x) {
			bottom_left = p2;
			bottom_right = p3;
		} else {
			bottom_left = p3;
			bottom_right = p2;
		}
		top = p1;
	}

	const int dx_left = abs(top.x - bottom_left.x);
	const int dy_left = abs(top.y - bottom_left.y);
	const int sx_left = (bottom_left.x < top.x) ? 1 : -1;
	const int sy_left = (bottom_left.y < top.y) ? 1 : -1;
	int err_left = dx_left - dy_left;

	const int dx_right = abs(top.x - bottom_right.x);
	const int dy_right = abs(top.y - bottom_right.y);
	const int sx_right = (bottom_right.x < top.x) ? 1 : -1;
	const int sy_right = (bottom_right.y < top.y) ? 1 : -1;
	int err_right = dx_left - dy_left;

	while (true) {
		// Set the current pixel
		coordinate_pair right = { bottom_right.x, bottom_right.y };
		coordinate_pair left = { bottom_left.x, bottom_left.y };

		//apollo_set_point(fb, point, color);
		apollo_draw_line(fb, left, right, color);

		// Check if the line is complete
		if (top.x == bottom_left.x && bottom_left.y == top.y) break;

		// Calculate the next pixel position
		int e2_left = 2 * err_left;
		if (e2_left > -dy_left) {
			err_left -= dy_left;
			bottom_left.x += sx_left;
		}
		if (e2_left < dx_left) {
			err_left += dx_left;
			bottom_left.y += sy_left;
		}

		int e2_right = 2 * err_right;
		if (e2_right > -dy_right) {
			err_right -= dy_right;
			bottom_right.x += sx_right;
		}
		if (e2_right < dx_right) {
			err_right += dx_right;
			bottom_right.y += sy_right;
		}
	}
}

void apollo_fill_triangle(framebuffer_t* fb, coordinate_pair* pairs, apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;

	apollo_color_t current_color = color;
	if (current_color.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &current_color);
	}

	/* General formula came from here:
	 * https://www.gabrielgambetta.com/computer-graphics-from-scratch/07-filled-triangles.html
	 */

	coordinate_pair p1 = pairs[0];
	coordinate_pair p2 = pairs[1];
	coordinate_pair p3 = pairs[2];

	// Sort vertices by y-coordinate
	if (p2.y < p1.y) swap(&p1, &p2);
	if (p3.y < p2.y) swap(&p2, &p3);
	if (p2.y < p1.y) swap(&p1, &p2);

	if (p2.y == p3.y) {
		for (int y = p1.y; y <= p3.y; y++) {
			int x_start = interpolate(p1, p2, y);
			int x_end = interpolate(p1, p3, y);
			apollo_draw_line(fb, (coordinate_pair) { x_start, y }, (coordinate_pair) { x_end, y }, color);
		}
	} else if (p1.y == p2.y) {
		for (int y = p1.y; y <= p3.y; y++) {
			int x_start = interpolate(p1, p3, y); // Interpolate x for v1->v3 edge
			int x_end = interpolate(p2, p3, y);   // Interpolate x for v2->v3 edge
			apollo_draw_line(fb, (coordinate_pair) { x_start, y }, (coordinate_pair) { x_end, y }, color);
		}
	} else {
		// General triangle - split into a flat-top and a flat-bottom triangle
		coordinate_pair p4 = {
			.x = p1.x + ((p2.y - p1.y) / (float) (p3.y - p1.y)) * (p3.x - p1.x),
			.y = p2.y
		};

		// Flat bottom with new coordinate
		for (int y = p1.y; y <= p4.y; y++) {
			int x_start = interpolate(p1, p2, y);
			int x_end = interpolate(p1, p4, y);
			apollo_draw_line(fb, (coordinate_pair) { x_start, y }, (coordinate_pair) { x_end, y }, color);
		}

		// Flat Top with new coordinate
		for (int y = p2.y; y <= p3.y; y++) {
			int x_start = interpolate(p2, p3, y);
			int x_end = interpolate(p4, p3, y);
			apollo_draw_line(fb, (coordinate_pair) { x_start, y }, (coordinate_pair) { x_end, y }, color);
		}
	}
}

void apollo_outline_triangle(framebuffer_t* fb, coordinate_pair* pairs, int border_width, apollo_color_t color) {
	(void) fb;
	(void) pairs;
	(void) border_width;
	(void) color;
}