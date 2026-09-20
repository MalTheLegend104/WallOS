#include <apollo.h>
#include <math.h>

// Most "proper" C implementations aren't supposed to define PI or M_PI
// Some do, so we don't want to redefine it to something else.
#ifndef PI
#define PI 3.14159265358979323846
#endif

void apollo_draw_polygon(framebuffer_t* fb, const coordinate_pair* pairs, const int pair_count, const apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;
	if (pair_count == 0 || pair_count == 1) return;
	if (pairs == NULL) return;

	bool self_closing = false;
	if (pairs[0].x == pairs[pair_count - 1].x && pairs[0].y == pairs[pair_count - 1].y) {
		self_closing = true;
	}

	// Draw the internal lines
	for (int i = 0; i < pair_count - 1; i++) {
		apollo_draw_line(fb, pairs[i], pairs[i + 1], color);
	}

	if (!self_closing) {
		apollo_draw_line(fb, pairs[0], pairs[pair_count - 1], color);
	}

}

void apollo_draw_polygon_n(framebuffer_t* fb, const coordinate_pair center, const int radius, const int sides, const apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;
	if (sides < 3) return;

	// Adjust angle for odd-sided polygons
	const double angle_offset = (sides % 2 == 0) ? 0.0 : PI / sides;
	const double angle_step = 2.0 * PI / sides;

	coordinate_pair prev_point = {
		center.x + (int) (radius * cos(angle_offset)),
		center.y + (int) (radius * sin(angle_offset))
	};

	for (int i = 1; i <= sides; i++) {
		const double angle = i * angle_step + angle_offset;
		const coordinate_pair next_point = {
			center.x + (int) (radius * cos(angle)),
			center.y + (int) (radius * sin(angle))
		};

		apollo_draw_line(fb, prev_point, next_point, color);
		prev_point = next_point;
	}
}

// TODO: Implement this. It requires polygon triangulation, and I really don't have time to figure out an algorithm for it.
void apollo_fill_polygon(framebuffer_t* fb, const coordinate_pair* pairs, int pair_count, apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;
	if (pair_count < 3) return;
	if (pairs == NULL) return;

	apollo_draw_polygon(fb, pairs, pair_count, color);
}