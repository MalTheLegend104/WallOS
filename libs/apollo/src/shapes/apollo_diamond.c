/**
 * @file apollo_diamond.c
 * @brief Diamonds implementation in WallOS Apollo
 * @version v0.1
 * @date 9/3/2024
 */
#include <apollo.h>

void apollo_draw_diamond(framebuffer_t* fb, coordinate_pair pair, int width, int height, apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;

	apollo_color_t current_color = color;
	if (color.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &current_color);
	}

	// The pair is the middle point.
	// We can simply add half the height and half the width to each direction

	int left_x = pair.x - width / 2;
	int top_y = pair.y - height / 2;

	int right_x = pair.x + width / 2;
	if (width % 2 != 0) right_x++; // Deal with odd width

	int bottom_y = pair.y + height / 2;
	if (height % 2 != 0) bottom_y++; // Deal with odd height

	// Top Left
	apollo_draw_line(fb, (coordinate_pair) { left_x, pair.y }, (coordinate_pair) { pair.x, top_y }, current_color);

	// Top Right
	apollo_draw_line(fb, (coordinate_pair) { right_x, pair.y }, (coordinate_pair) { pair.x, top_y }, current_color);

	// Bottom Left
	apollo_draw_line(fb, (coordinate_pair) { left_x, pair.y }, (coordinate_pair) { pair.x, bottom_y }, current_color);

	// Bottom Right
	apollo_draw_line(fb, (coordinate_pair) { right_x, pair.y }, (coordinate_pair) { pair.x, bottom_y }, current_color);
}

void apollo_fill_diamond(framebuffer_t* fb, coordinate_pair pair, int width, int height, apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;

	apollo_color_t current_color = color;
	if (color.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &current_color);
	}

	int left_x = pair.x - width / 2;
	int top_y = pair.y - height / 2;

	int right_x = pair.x + width / 2;
	if (width % 2 != 0) right_x++; // Deal with odd width

	int bottom_y = pair.y + height / 2;
	if (height % 2 != 0) bottom_y++; // Deal with odd height

	// We're going to draw it as two triangles.
	coordinate_pair top[3] = {
		{pair.x, top_y},
		{left_x, pair.y},
		{right_x, pair.y}
	};

	coordinate_pair bottom[3] = {
		{pair.x, bottom_y},
		{right_x, pair.y},
		{left_x, pair.y}
	};

	// Top Triangle
	apollo_fill_triangle(fb, top, current_color);
//	apollo_draw_triangle(fb, top, current_color);

	// Bottom Triangle
	apollo_fill_triangle(fb, bottom, current_color);
//	apollo_draw_triangle(fb, bottom, current_color);
}

void apollo_outline_diamond(framebuffer_t* fb, coordinate_pair pair, int width, int height, int border_width, apollo_color_t color) {
	(void) pair;
	(void) width;
	(void) height;
	(void) border_width;

	if (fb == NULL || fb->info == NULL) return;

	apollo_color_t current_color = color;
	if (color.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &current_color);
	}


}