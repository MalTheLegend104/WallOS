/**
 * @file apollo_circle.c
 * @brief Handles all the font printing related to Apollo
 * @version v0.1
 * @date 9/3/2024
 */
#include <apollo.h>
#include <math.h>

void apollo_draw_circle(framebuffer_t* fb, coordinate_pair pair, int radius, apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;

	apollo_color_t current_color = color;
	if (current_color.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &current_color);
	}

	// This uses Bresenham’s circle drawing algorithm
	// It essentially draws 1/8 of the circle and reflects it in the other 7 other octants
	// It approximates the equation x^2 + y^2 = r^2
	int x = 0, y = radius;
	int d = 3 - 2 * radius;

	while (y >= x) {
		apollo_set_point(fb, (coordinate_pair) { pair.x + x, pair.y + y }, current_color);
		apollo_set_point(fb, (coordinate_pair) { pair.x - x, pair.y + y }, current_color);
		apollo_set_point(fb, (coordinate_pair) { pair.x + x, pair.y - y }, current_color);
		apollo_set_point(fb, (coordinate_pair) { pair.x - x, pair.y - y }, current_color);
		apollo_set_point(fb, (coordinate_pair) { pair.x + y, pair.y + x }, current_color);
		apollo_set_point(fb, (coordinate_pair) { pair.x - y, pair.y + x }, current_color);
		apollo_set_point(fb, (coordinate_pair) { pair.x + y, pair.y - x }, current_color);
		apollo_set_point(fb, (coordinate_pair) { pair.x - y, pair.y - x }, current_color);

		if (d > 0) {
			y--;
			d = d + 4 * (x - y) + 10;
		} else {
			d = d + 4 * x + 6;
		}

		x++;
	}
}

void apollo_fill_circle(framebuffer_t* fb, coordinate_pair pair, int radius, apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;

	apollo_color_t current_color = color;
	if (current_color.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &current_color);
	}

	int x = 0, y = radius;
	int d = 3 - 2 * radius;

	while (y >= x) {
		// Draw horizontal lines between the left-most and right-most points at each y
		// for (int i = -x; i <= x; i++) {
		// 	apollo_set_point(fb, (coordinate_pair) {pair.x + i, pair.y + y}, current_color);
		// 	apollo_set_point(fb, (coordinate_pair) {pair.x + i, pair.y - y}, current_color);
		// }
		// for (int i = -y; i <= y; i++) {
		// 	apollo_set_point(fb, (coordinate_pair) {pair.x + i, pair.y + x}, current_color);
		// 	apollo_set_point(fb, (coordinate_pair) {pair.x + i, pair.y - x}, current_color);
		// }

		apollo_draw_line(fb,
			(coordinate_pair) {			
pair.x - x, pair.y + y
		},
			(coordinate_pair) {
			pair.x + x, pair.y + y
		},
			current_color);
		apollo_draw_line(fb,
			(coordinate_pair) {
			pair.x - x, pair.y - y
		},
			(coordinate_pair) {
			pair.x + x, pair.y - y		
},
			current_color);
		apollo_draw_line(fb,
			(coordinate_pair) {
			pair.x - y, pair.y + x
		},
			(coordinate_pair) {
			pair.x + y, pair.y + x
		},
			current_color);
		apollo_draw_line(fb,
			(coordinate_pair) {
			pair.x - y, pair.y - x
		},
			(coordinate_pair) {
			pair.x + y, pair.y - x
		},
			current_color);

		if (d > 0) {
			y--;
			d = d + 4 * (x - y) + 10;
		} else {
			d = d + 4 * x + 6;
		}

		x++;
	}
}

// This is wildly inaccurate. There is probably an algorithm out there somewhere to do this.
// I spent a few hours looking and I couldn't find one, and I really dont want to put the time into figuring out how to do it.
void apollo_outline_circle(framebuffer_t* fb, coordinate_pair pair, int border_width, int radius, apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;

	// Convert colors to framebuffer's color type if needed
	apollo_color_t current_color = color;
	if (current_color.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &current_color);
	}

	for (int i = 0; i < border_width; i++) {
		apollo_draw_circle(fb, pair, radius + i, current_color);
	}
}