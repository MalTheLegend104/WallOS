/**
 * @file apollo_utility.c
 * @brief All utility functions in the WallOS Apollo GFX Framework
 * @version v0.1
 * @date 8/29/2024
 */

#include <apollo.h>
#include <string.h>

// TODO: uncomment this
// #ifdef APOLLO_DEBUG
#include <stdio.h>
void apollo_print_pixel_type(apollo_pixel_type type) {
	switch (type) {
		case APOLLO_PIXEL_TYPE_UNKNOWN:
			printf("Unknown Pixel Type");
			break;
		case APOLLO_PIXEL_TYPE_RGB32:
			printf("RGB 32-bit");
			break;
		case APOLLO_PIXEL_TYPE_RGBA32:
			printf("RGBA 32-bit");
			break;
		case APOLLO_PIXEL_TYPE_BGR32:
			printf("BGR 32-bit");
			break;
		case APOLLO_PIXEL_TYPE_BGRA32:
			printf("BGRA 32-bit");
			break;
		case APOLLO_PIXEL_TYPE_RGB16_565:
			printf("RGB 16-bit (5-6-5)");
			break;
		case APOLLO_PIXEL_TYPE_RGB16_555:
			printf("RGB 16-bit (5-5-5)");
			break;
		case APOLLO_PIXEL_TYPE_BGR16_565:
			printf("BGR 16-bit (5-6-5)");
			break;
		case APOLLO_PIXEL_TYPE_BGR16_555:
			printf("BGR 16-bit (5-5-5)");
			break;
		case APOLLO_PIXEL_TYPE_RGB24:
			printf("RGB 24-bit");
			break;
		case APOLLO_PIXEL_TYPE_BGR24:
			printf("BGR 24-bit");
			break;
		default:
			printf("Invalid Pixel Type");
			break;
	}
}

void apollo_print_info(framebuffer_info_t* info) {
	printf("Framebuffer Info:\n");
	printf("\tWidth: %i\n", info->width);
	printf("\tHeight: %i\n", info->height);
	printf("\tPitch: %i\n", info->pitch);
	printf("\tPixel Width: %i\n", info->pixel_width);
	printf("\t");
	apollo_print_pixel_type(info->type);
	printf("\n");
}
// #endif

void apollo_switch_color(apollo_pixel_type target_type, apollo_color_t* color) {
	if (color == NULL) return;
	if (color->type == target_type) return;

	/* The math for all these conversions is as follows:
	 *
	 * new_color = (current color * new_color_max) / current_color_max
	 *
	 * It's essentially taking the average of the color using the new max.
	 * It estimates the new one by saying "the old one is approximately this spot in the range of possible values."
	 */

	const uint8_t max_2bit = 3;		// Max value of 2 bit color
	const uint8_t max_3bit = 7;		// Max value of 3 bit color
	const uint8_t max_4bit = 15;   	// Max value of 4 bit color
	const uint8_t max_5bit = 31;   	// Max value of 5 bit color
	const uint8_t max_6bit = 63;   	// Max value of 6 bit color
	const uint8_t max_8bit = 255;  	// Max value of 8 bit color

	// This is a switch case with nested switch cases.
	// It looks horrible, but is easy to follow.
	switch (target_type) {
		// It doesn't really matter if we convert alpha or not on those that don't have it.
		// It's zero anyway, and we don't really care about preservation of color fidelity.
		case APOLLO_PIXEL_TYPE_ARGB8888:
		case APOLLO_PIXEL_TYPE_RGB32:
		case APOLLO_PIXEL_TYPE_BGR32:
		case APOLLO_PIXEL_TYPE_RGBA32:
		case APOLLO_PIXEL_TYPE_BGRA32:
			switch (color->type) {
				case APOLLO_PIXEL_TYPE_RGB16_565:
				case APOLLO_PIXEL_TYPE_BGR16_565:
					color->red = (color->red * max_8bit) / max_5bit;
					color->green = (color->green * max_8bit) / max_6bit;
					color->blue = (color->blue * max_8bit) / max_5bit;
					break;

				case APOLLO_PIXEL_TYPE_RGB16_555:
				case APOLLO_PIXEL_TYPE_BGR16_555:
					color->red = (color->red * max_8bit) / max_5bit;
					color->green = (color->green * max_8bit) / max_5bit;
					color->blue = (color->blue * max_8bit) / max_5bit;
					break;

				case APOLLO_PIXEL_TYPE_ARGB4444:
				case APOLLO_PIXEL_TYPE_RGBA4444:
				case APOLLO_PIXEL_TYPE_ABGR4444:
				case APOLLO_PIXEL_TYPE_BGRA4444:
					// We convert alpha even if it doesn't matter
					color->red = (color->red * max_8bit) / max_4bit;
					color->green = (color->green * max_8bit) / max_4bit;
					color->blue = (color->blue * max_8bit) / max_4bit;
					color->alpha = (color->alpha * max_8bit) / max_4bit;
					break;

				case APOLLO_PIXEL_TYPE_RGB332:
					color->red = (color->red * max_8bit) / max_3bit;
					color->green = (color->green * max_8bit) / max_3bit;
					color->blue = (color->blue * max_8bit) / max_2bit;
					break;

				// 24-bit and 32-bit are already the same values. No conversion needed.
				default: break;
			}
			break;

		case APOLLO_PIXEL_TYPE_RGB24:
		case APOLLO_PIXEL_TYPE_BGR24:
			switch (color->type) {
				case APOLLO_PIXEL_TYPE_RGB16_565:
				case APOLLO_PIXEL_TYPE_BGR16_565:
					color->red = (color->red * max_8bit) / max_5bit;
					color->green = (color->green * max_8bit) / max_6bit;
					color->blue = (color->blue * max_8bit) / max_5bit;
					break;

				case APOLLO_PIXEL_TYPE_RGB16_555:
				case APOLLO_PIXEL_TYPE_BGR16_555:
					color->red = (color->red * max_8bit) / max_5bit;
					color->green = (color->green * max_8bit) / max_5bit;
					color->blue = (color->blue * max_8bit) / max_5bit;
					break;

				case APOLLO_PIXEL_TYPE_ARGB4444:
				case APOLLO_PIXEL_TYPE_RGBA4444:
				case APOLLO_PIXEL_TYPE_ABGR4444:
				case APOLLO_PIXEL_TYPE_BGRA4444:
					// 24-bit doesnt have an alpha channel
					color->red = (color->red * max_8bit) / max_4bit;
					color->green = (color->green * max_8bit) / max_4bit;
					color->blue = (color->blue * max_8bit) / max_4bit;
					break;

				case APOLLO_PIXEL_TYPE_RGB332:
					color->red = (color->red * max_8bit) / max_3bit;
					color->green = (color->green * max_8bit) / max_3bit;
					color->blue = (color->blue * max_8bit) / max_2bit;
					break;

				// 24-bit and 32-bit are already the same values. No conversion needed.
				default: break;
			}
			break;

		case APOLLO_PIXEL_TYPE_RGB16_565:
		case APOLLO_PIXEL_TYPE_BGR16_565:
			switch (color->type) {
				case APOLLO_PIXEL_TYPE_ARGB8888:
				case APOLLO_PIXEL_TYPE_RGB32:
				case APOLLO_PIXEL_TYPE_RGBA32:
				case APOLLO_PIXEL_TYPE_BGR32:
				case APOLLO_PIXEL_TYPE_BGRA32:
				case APOLLO_PIXEL_TYPE_RGB24:
				case APOLLO_PIXEL_TYPE_BGR24:
					color->red = (color->red * max_5bit) / max_8bit;
					color->green = (color->green * max_6bit) / max_8bit;
					color->blue = (color->blue * max_5bit) / max_8bit;
					break;

				case APOLLO_PIXEL_TYPE_RGB16_555:
				case APOLLO_PIXEL_TYPE_BGR16_555:
					color->red = (color->red * max_5bit) / max_5bit;
					color->green = (color->green * max_6bit) / max_5bit;
					color->blue = (color->blue * max_5bit) / max_5bit;
					break;

				case APOLLO_PIXEL_TYPE_ARGB4444:
				case APOLLO_PIXEL_TYPE_RGBA4444:
				case APOLLO_PIXEL_TYPE_ABGR4444:
				case APOLLO_PIXEL_TYPE_BGRA4444:
					color->red = (color->red * max_5bit) / max_4bit;
					color->green = (color->green * max_6bit) / max_4bit;
					color->blue = (color->blue * max_5bit) / max_4bit;
					break;

				case APOLLO_PIXEL_TYPE_RGB332:
					color->red = (color->red * max_5bit) / max_3bit;
					color->green = (color->green * max_6bit) / max_3bit;
					color->blue = (color->blue * max_5bit) / max_2bit;
					break;
				// Same type
				default: break;
			}
			break;

		case APOLLO_PIXEL_TYPE_RGB16_555:
		case APOLLO_PIXEL_TYPE_BGR16_555:
			switch (color->type) {
				case APOLLO_PIXEL_TYPE_ARGB8888:
				case APOLLO_PIXEL_TYPE_RGB32:
				case APOLLO_PIXEL_TYPE_RGBA32:
				case APOLLO_PIXEL_TYPE_BGR32:
				case APOLLO_PIXEL_TYPE_BGRA32:
				case APOLLO_PIXEL_TYPE_RGB24:
				case APOLLO_PIXEL_TYPE_BGR24:
					color->red = (color->red * max_5bit) / max_8bit;
					color->green = (color->green * max_5bit) / max_8bit;
					color->blue = (color->blue * max_5bit) / max_8bit;
					break;

				case APOLLO_PIXEL_TYPE_RGB16_565:
				case APOLLO_PIXEL_TYPE_BGR16_565:
					color->red = (color->red * max_5bit) / max_5bit;
					color->green = (color->green * max_5bit) / max_6bit;
					color->blue = (color->blue * max_5bit) / max_5bit;
					break;

				case APOLLO_PIXEL_TYPE_ARGB4444:
				case APOLLO_PIXEL_TYPE_RGBA4444:
				case APOLLO_PIXEL_TYPE_ABGR4444:
				case APOLLO_PIXEL_TYPE_BGRA4444:
					color->red = (color->red * max_5bit) / max_4bit;
					color->green = (color->green * max_5bit) / max_4bit;
					color->blue = (color->blue * max_5bit) / max_4bit;
					break;

				case APOLLO_PIXEL_TYPE_RGB332:
					color->red = (color->red * max_5bit) / max_3bit;
					color->green = (color->green * max_5bit) / max_3bit;
					color->blue = (color->blue * max_5bit) / max_2bit;
					break;
				// Same type
				default: break;
			}
			break;

		case APOLLO_PIXEL_TYPE_ARGB4444:
		case APOLLO_PIXEL_TYPE_RGBA4444:
		case APOLLO_PIXEL_TYPE_ABGR4444:
		case APOLLO_PIXEL_TYPE_BGRA4444:
			switch (color->type) {
				case APOLLO_PIXEL_TYPE_ARGB8888:
				case APOLLO_PIXEL_TYPE_RGB32:
				case APOLLO_PIXEL_TYPE_RGBA32:
				case APOLLO_PIXEL_TYPE_BGR32:
				case APOLLO_PIXEL_TYPE_BGRA32:
				case APOLLO_PIXEL_TYPE_RGB24:
				case APOLLO_PIXEL_TYPE_BGR24:
					color->red = (color->red * max_4bit) / max_8bit;
					color->green = (color->green * max_4bit) / max_8bit;
					color->blue = (color->blue * max_4bit) / max_8bit;
					color->alpha = (color->alpha * max_4bit) / max_8bit;
					break;

				case APOLLO_PIXEL_TYPE_RGB16_565:
				case APOLLO_PIXEL_TYPE_BGR16_565:
					color->red = (color->red * max_4bit) / max_5bit;
					color->green = (color->green * max_4bit) / max_6bit;
					color->blue = (color->blue * max_4bit) / max_5bit;
					break;

				case APOLLO_PIXEL_TYPE_RGB16_555:
				case APOLLO_PIXEL_TYPE_BGR16_555:
					color->red = (color->red * max_4bit) / max_5bit;
					color->green = (color->green * max_4bit) / max_5bit;
					color->blue = (color->blue * max_4bit) / max_5bit;
					break;

				case APOLLO_PIXEL_TYPE_RGB332:
					color->red = (color->red * max_4bit) / max_3bit;
					color->green = (color->green * max_4bit) / max_3bit;
					color->blue = (color->blue * max_4bit) / max_2bit;
					break;

				// Converting between the same type is useless.
				default: break;
			}
			break;

		case APOLLO_PIXEL_TYPE_RGB332:
			// These are different enough I need to check all of them again
			// We're just flat out going to pretend alpha isn't a thing.
			// 8-bit colors dont have alpha.
			switch (color->type) {
				case APOLLO_PIXEL_TYPE_ARGB8888:
				case APOLLO_PIXEL_TYPE_RGB32:
				case APOLLO_PIXEL_TYPE_RGBA32:
				case APOLLO_PIXEL_TYPE_BGR32:
				case APOLLO_PIXEL_TYPE_BGRA32:
				case APOLLO_PIXEL_TYPE_RGB24:
				case APOLLO_PIXEL_TYPE_BGR24:
					// 32-bit and 24-bit conversions
					color->red = (color->red * max_3bit) / max_8bit;
					color->green = (color->green * max_3bit) / max_8bit;
					color->blue = (color->blue * max_2bit) / max_8bit;
					break;

				case APOLLO_PIXEL_TYPE_RGB16_565:
				case APOLLO_PIXEL_TYPE_BGR16_565:
					// 565 conversions
					color->red = (color->red * max_3bit) / max_5bit;
					color->green = (color->green * max_3bit) / max_6bit;
					color->blue = (color->blue * max_2bit) / max_5bit;
					break;
				case APOLLO_PIXEL_TYPE_RGB16_555:
				case APOLLO_PIXEL_TYPE_BGR16_555:
					// 555 conversions
					color->red = (color->red * max_3bit) / max_5bit;
					color->green = (color->green * max_3bit) / max_5bit;
					color->blue = (color->blue * max_2bit) / max_5bit;
					break;

				case APOLLO_PIXEL_TYPE_ARGB4444:
				case APOLLO_PIXEL_TYPE_RGBA4444:
				case APOLLO_PIXEL_TYPE_ABGR4444:
				case APOLLO_PIXEL_TYPE_BGRA4444:
					// 4444 conversions
					color->red = (color->red * max_3bit) / max_4bit;
					color->green = (color->green * max_3bit) / max_4bit;
					color->blue = (color->blue * max_2bit) / max_4bit;
					break;
				default: break;
			}
			// Convert to 8-bit with 3-3-2 format
			// color->red = (color->red * 7) / max_8bit;
			// color->green = (color->green * 7) / max_8bit;
			// color->blue = (color->blue * 3) / max_8bit;
			break;

		default:
			color->type = APOLLO_PIXEL_TYPE_UNKNOWN;
			return;
	}

	color->type = target_type;
}

void apollo_set_pixel(const framebuffer_info_t* info, uint8_t* pixel, const apollo_color_t color) {
	if (pixel == NULL || info == NULL) return;
	apollo_color_t current_color = color;

	if (color.type != info->type) {
		apollo_print_pixel_type(color.type);
		apollo_print_pixel_type(info->type);
		apollo_switch_color(info->type, &current_color);
	}

	// This is a mess of bit-shifting, and I'm sorry
	// This is by far the easiest way to deal with pixel data.
	switch (info->type) {
		case APOLLO_PIXEL_TYPE_UNKNOWN:
			// Handle unknown pixel type
			// We do nothing for now (or probably ever)
			break;

		case APOLLO_PIXEL_TYPE_RGB32:
			// 32-bit RGB: 8 bits each for red, green, and blue
			// *(uint32_t*)pixel = (current_color.red << 16) | (current_color.green << 8) | current_color.blue;
			pixel[0] = current_color.red;
			pixel[1] = current_color.green;
			pixel[2] = current_color.blue;
			pixel[3] = 0;
			break;

		case APOLLO_PIXEL_TYPE_ARGB8888:
			// 32-bit ARGB: 8 bits each for red, green, blue, and alpha
			// pixel[0] = current_color.alpha;
			// pixel[1] = current_color.red;
			// pixel[2] = current_color.green;
			// pixel[3] = current_color.blue;
			pixel[0] = current_color.blue;
			pixel[1] = current_color.green;
			pixel[2] = current_color.red;
			pixel[3] = current_color.alpha;
			break;

		case APOLLO_PIXEL_TYPE_RGBA32:
			// 32-bit RGBA: 8 bits each for red, green, blue, and alpha
			// *(uint32_t*)pixel = (current_color.alpha << 24) | (current_color.red << 16) | (current_color.green << 8) | current_color.blue;
			pixel[0] = current_color.red;
			pixel[1] = current_color.green;
			pixel[2] = current_color.blue;
			pixel[3] = current_color.alpha;
			break;

		case APOLLO_PIXEL_TYPE_BGR32:
			// 32-bit BGR: 8 bits each for blue, green, and red
			//*(uint32_t*)pixel = (current_color.blue << 16) | (current_color.green << 8) | current_color.red;
			pixel[0] = current_color.blue;
			pixel[1] = current_color.green;
			pixel[2] = current_color.red;
			pixel[3] = 0;
			break;

		case APOLLO_PIXEL_TYPE_BGRA32:
			// 32-bit BGRA: 8 bits each for blue, green, red, and alpha
			//*(uint32_t*)pixel = (current_color.alpha << 24) | (current_color.blue << 16) | (current_color.green << 8) | current_color.red;
			pixel[0] = current_color.blue;
			pixel[1] = current_color.green;
			pixel[2] = current_color.red;
			pixel[3] = current_color.alpha;
			break;

		case APOLLO_PIXEL_TYPE_RGB24:
			// 24-bit RGB: 8 bits each for red, green, and blue (no alpha)
			pixel[0] = current_color.red;
			pixel[1] = current_color.green;
			pixel[2] = current_color.blue;
			break;

		case APOLLO_PIXEL_TYPE_BGR24:
			// 24-bit BGR: 8 bits each for blue, green, and red (no alpha)
			pixel[0] = current_color.blue;
			pixel[1] = current_color.green;
			pixel[2] = current_color.red;
			break;

		// 16-bit is only a little cursed, at least with the 5's and 6's
		// The easiest way to do it is to cast the pointer to uint16_t array and bit shift
		case APOLLO_PIXEL_TYPE_RGB16_565:
			// 16-bit RGB: 5 bits for red, 6 bits for green, 5 bits for blue
			*(uint16_t*) pixel = ((current_color.red & 0x1F) << 11) | ((current_color.green & 0x3F) << 5) | (current_color.blue & 0x1F);
			break;

		case APOLLO_PIXEL_TYPE_RGB16_555:
			// 16-bit RGB: 5 bits each for red, green, and blue
			*(uint16_t*) pixel = ((current_color.red & 0x1F) << 10) | ((current_color.green & 0x1F) << 5) | (current_color.blue & 0x1F);
			break;

		case APOLLO_PIXEL_TYPE_BGR16_565:
			// 16-bit BGR: 5 bits for blue, 6 bits for green, 5 bits for red
			*(uint16_t*) pixel = ((current_color.blue & 0x1F) << 11) | ((current_color.green & 0x3F) << 5) | (current_color.red & 0x1F);
			break;

		case APOLLO_PIXEL_TYPE_BGR16_555:
			// 16-bit BGR: 5 bits each for blue, green, and red
			*(uint16_t*) pixel = ((current_color.blue & 0x1F) << 10) | ((current_color.green & 0x1F) << 5) | (current_color.red & 0x1F);
			break;

		case APOLLO_PIXEL_TYPE_ARGB4444:
			pixel[0] = ((current_color.alpha & 0xF) << 4) | (current_color.red & 0xF);
			pixel[1] = ((current_color.green & 0xF) << 4) | (current_color.blue & 0xF);
			break;

		case APOLLO_PIXEL_TYPE_ABGR4444:
			pixel[0] = ((current_color.alpha & 0xF) << 4) | (current_color.blue & 0xF);
			pixel[1] = ((current_color.green & 0xF) << 4) | (current_color.red & 0xF);
			break;

		case APOLLO_PIXEL_TYPE_RGBA4444:
			pixel[0] = ((current_color.red & 0xF) << 4) | (current_color.green & 0xF);
			pixel[1] = ((current_color.blue & 0xF) << 4) | (current_color.alpha & 0xF);
			break;

		case APOLLO_PIXEL_TYPE_BGRA4444:
			pixel[0] = ((current_color.blue & 0xF) << 4) | (current_color.green & 0xF);
			pixel[1] = ((current_color.red & 0xF) << 4) | (current_color.alpha & 0xF);
			break;

		// The only support 8 bit format
		case APOLLO_PIXEL_TYPE_RGB332:
			// 8-bit RGB: 3 bits for red, 3 bits for green, 2 bits for blue (no alpha)
			pixel[0] = ((current_color.red & 0x7) << 5) | ((current_color.green & 0x7) << 2) | (current_color.blue & 0x3);
			break;
	}
}

uint8_t* apollo_get_point(framebuffer_t* fb, coordinate_pair pair) {
	if (fb == NULL || fb->info == NULL) {
	//	printf("Null Framebuffer Info\n");
		return NULL;
	}
	if (pair.x > fb->info->pitch || pair.y > fb->info->height) {
	//	printf("Invalid point\n");
		return NULL;
	}

	const int location = ((pair.y) * (fb->info->pitch)) + (pair.x * fb->info->pixel_width);

	return (fb->buffer + location);
}

void apollo_set_point(framebuffer_t* fb, coordinate_pair p, apollo_color_t color) {
	apollo_set_pixel(fb->info, apollo_get_point(fb, p), color);
}

void apollo_fill_buffer(framebuffer_t* fb, apollo_color_t color) {
	if (fb == NULL || fb->info == NULL) return;

	apollo_color_t current_color = color;

	if (color.type != fb->info->type) {
		apollo_switch_color(fb->info->type, &current_color);
	}

	size_t framebuffer_length = (fb->info->height * fb->info->width * fb->info->pixel_width);

	size_t index = 0;

	uint16_t full_color;
	uint8_t lower_half;
	uint8_t upper_half;

	switch (fb->info->type) {
		case APOLLO_PIXEL_TYPE_UNKNOWN:
			// Handle unknown pixel type
			// We do nothing for now (or probably ever)
			break;

		case APOLLO_PIXEL_TYPE_RGB32:
			while (index < framebuffer_length - sizeof(uint32_t)) {
				fb->buffer[index] = current_color.red;
				fb->buffer[index + 1] = current_color.green;
				fb->buffer[index + 2] = current_color.blue;
				fb->buffer[index + 3] = 0;
				index += sizeof(uint32_t);
			}
			break;

		case APOLLO_PIXEL_TYPE_ARGB8888:
			while (index < framebuffer_length - sizeof(uint32_t)) {
				fb->buffer[index] = current_color.alpha;
				fb->buffer[index + 1] = current_color.red;
				fb->buffer[index + 2] = current_color.green;
				fb->buffer[index + 3] = current_color.blue;
				index += sizeof(uint32_t);
			}
			break;

		case APOLLO_PIXEL_TYPE_RGBA32:
			while (index < framebuffer_length - sizeof(uint32_t)) {
				fb->buffer[index] = current_color.red;
				fb->buffer[index + 1] = current_color.green;
				fb->buffer[index + 2] = current_color.blue;
				fb->buffer[index + 3] = current_color.alpha;
				index += sizeof(uint32_t);
			}
			break;

		case APOLLO_PIXEL_TYPE_BGR32:
			while (index < framebuffer_length - sizeof(uint32_t)) {
				fb->buffer[index] = current_color.blue;
				fb->buffer[index + 1] = current_color.green;
				fb->buffer[index + 2] = current_color.red;
				fb->buffer[index + 3] = 0;
				index += sizeof(uint32_t);
			}
			break;

		case APOLLO_PIXEL_TYPE_BGRA32:
			while (index < framebuffer_length - sizeof(uint32_t)) {
				fb->buffer[index] = current_color.blue;
				fb->buffer[index + 1] = current_color.green;
				fb->buffer[index + 2] = current_color.red;
				fb->buffer[index + 3] = current_color.alpha;
				index += sizeof(uint32_t);
			}
			break;

		case APOLLO_PIXEL_TYPE_RGB24:
			// 24-bit RGB: 8 bits each for red, green, and blue (no alpha)
			while (index < framebuffer_length - (sizeof(uint8_t) * 3)) {
				fb->buffer[index] = current_color.red;
				fb->buffer[index + 1] = current_color.green;
				fb->buffer[index + 2] = current_color.blue;
				index += (sizeof(uint8_t) * 3);
			}
			break;

		case APOLLO_PIXEL_TYPE_BGR24:
			// 24-bit BGR: 8 bits each for blue, green, and red (no alpha)
			while (index < framebuffer_length - (sizeof(uint8_t) * 3)) {
				fb->buffer[index] = current_color.blue;
				fb->buffer[index + 1] = current_color.green;
				fb->buffer[index + 2] = current_color.red;
				index += (sizeof(uint8_t) * 3);
			}
			break;

			// 16-bit is only a little cursed, at least with the 5's and 6's
			// The easiest way to do it is to cast the pointer to uint16_t array and bit shift
		case APOLLO_PIXEL_TYPE_RGB16_565:
			// 16-bit RGB: 5 bits for red, 6 bits for green, 5 bits for blue
			full_color = ((current_color.red & 0x1F) << 11) | ((current_color.green & 0x3F) << 5) | (current_color.blue & 0x1F);
			lower_half = full_color & 0xFFFF;
			upper_half = (full_color >> 8) & 0xFFFF;
			while (index < framebuffer_length - sizeof(uint16_t)) {
				fb->buffer[index] = lower_half;
				fb->buffer[index + 1] = upper_half;
				index += sizeof(uint16_t);
			}
			break;

		case APOLLO_PIXEL_TYPE_RGB16_555:
			full_color = ((current_color.red & 0x1F) << 10) | ((current_color.green & 0x1F) << 5) | (current_color.blue & 0x1F);
			lower_half = full_color & 0xFFFF;
			upper_half = (full_color >> 8) & 0xFFFF;
			while (index < framebuffer_length - sizeof(uint16_t)) {
				fb->buffer[index] = lower_half;
				fb->buffer[index + 1] = upper_half;
				index += sizeof(uint16_t);
			}
			break;

		case APOLLO_PIXEL_TYPE_BGR16_565:
			// 16-bit BGR: 5 bits for blue, 6 bits for green, 5 bits for red
			full_color = ((current_color.blue & 0x1F) << 11) | ((current_color.green & 0x3F) << 5) | (current_color.red & 0x1F);
			lower_half = full_color & 0xFFFF;
			upper_half = (full_color >> 8) & 0xFFFF;
			while (index < framebuffer_length - sizeof(uint16_t)) {
				fb->buffer[index] = lower_half;
				fb->buffer[index + 1] = upper_half;
				index += sizeof(uint16_t);
			}
			break;

		case APOLLO_PIXEL_TYPE_BGR16_555:
			// 16-bit BGR: 5 bits each for blue, green, and red
			full_color = ((current_color.blue & 0x1F) << 10) | ((current_color.green & 0x1F) << 5) | (current_color.red & 0x1F);
			lower_half = full_color & 0xFFFF;
			upper_half = (full_color >> 8) & 0xFFFF;
			while (index < framebuffer_length - sizeof(uint16_t)) {
				fb->buffer[index] = lower_half;
				fb->buffer[index + 1] = upper_half;
				index += sizeof(uint16_t);
			}
			break;

		case APOLLO_PIXEL_TYPE_ARGB4444:
			upper_half = ((current_color.alpha & 0xF) << 4) | (current_color.red & 0xF);
			lower_half = ((current_color.green & 0xF) << 4) | (current_color.blue & 0xF);
			while (index < framebuffer_length - sizeof(uint16_t)) {
				fb->buffer[index] = lower_half;
				fb->buffer[index + 1] = upper_half;
				index += sizeof(uint16_t);
			}
			break;

		case APOLLO_PIXEL_TYPE_ABGR4444:
			upper_half = ((current_color.alpha & 0xF) << 4) | (current_color.blue & 0xF);
			lower_half = ((current_color.green & 0xF) << 4) | (current_color.red & 0xF);
			while (index < framebuffer_length - sizeof(uint16_t)) {
				fb->buffer[index] = lower_half;
				fb->buffer[index + 1] = upper_half;
				index += sizeof(uint16_t);
			}
			break;

		case APOLLO_PIXEL_TYPE_RGBA4444:
			upper_half = ((current_color.red & 0xF) << 4) | (current_color.green & 0xF);
			lower_half = ((current_color.blue & 0xF) << 4) | (current_color.alpha & 0xF);
			while (index < framebuffer_length - sizeof(uint16_t)) {
				fb->buffer[index] = lower_half;
				fb->buffer[index + 1] = upper_half;
				index += sizeof(uint16_t);
			}
			break;

		case APOLLO_PIXEL_TYPE_BGRA4444:
			upper_half = ((current_color.blue & 0xF) << 4) | (current_color.green & 0xF);
			lower_half = ((current_color.red & 0xF) << 4) | (current_color.alpha & 0xF);
			while (index < framebuffer_length - sizeof(uint16_t)) {
				fb->buffer[index] = lower_half;
				fb->buffer[index + 1] = upper_half;
				index += sizeof(uint16_t);
			}
			break;

		case APOLLO_PIXEL_TYPE_RGB332:
			// This is nice, we can just memset it.
			const int value = ((current_color.red & 0x7) << 5) | ((current_color.green & 0x7) << 2) | (current_color.blue & 0x3);
			memset(fb->buffer, value, framebuffer_length);

		default: break;
	}
}

apollo_color_t apollo_get_color(size_t color, apollo_pixel_type type) {
	// Clang tidy complains about identical branches (I have no clue why).

	switch (type) {
		// Unknowns are just going to be treated like 8 bit channels
		case APOLLO_PIXEL_TYPE_UNKNOWN:
		case APOLLO_PIXEL_TYPE_ARGB8888:
			return (apollo_color_t) {
				(uint8_t) ((color >> 24) & 0xFF),
					(uint8_t) ((color >> 16) & 0xFF),
					(uint8_t) ((color >> 8) & 0xFF),
					(uint8_t) ((color & 0xFF)),
					type
			};
		case APOLLO_PIXEL_TYPE_RGB32:
			return (apollo_color_t) {
				0,
					(uint8_t) ((color >> 24) & 0xFF),
					(uint8_t) ((color >> 16) & 0xFF),
					(uint8_t) ((color >> 8) & 0xFF),
					type
			};

		case APOLLO_PIXEL_TYPE_RGBA32:
			return (apollo_color_t) {

				(uint8_t) ((color & 0xFF)),
					(uint8_t) ((color >> 24) & 0xFF),
					(uint8_t) ((color >> 16) & 0xFF),
					(uint8_t) ((color >> 8) & 0xFF),
					type
			};

		case APOLLO_PIXEL_TYPE_BGR32:
			return (apollo_color_t) {

				0,
					(uint8_t) ((color >> 8) & 0xFF),
					(uint8_t) ((color >> 16) & 0xFF),
					(uint8_t) ((color >> 24) & 0xFF),
					type
			};

		case APOLLO_PIXEL_TYPE_BGRA32:
			return (apollo_color_t) {

				(uint8_t) ((color & 0xFF)),
					(uint8_t) ((color >> 8) & 0xFF),
					(uint8_t) ((color >> 16) & 0xFF),
					(uint8_t) ((color >> 24) & 0xFF),
					type
			};

		case APOLLO_PIXEL_TYPE_RGB24:
			return (apollo_color_t) {

				0,
					(uint8_t) ((color >> 16) & 0xFF),
					(uint8_t) ((color >> 8) & 0xFF),
					(uint8_t) ((color & 0xFF)),
					type
			};

		case APOLLO_PIXEL_TYPE_BGR24:
			return (apollo_color_t) {

				0,
					(uint8_t) ((color & 0xFF)),
					(uint8_t) ((color >> 8) & 0xFF),
					(uint8_t) ((color >> 16) & 0xFF),
					type
			};

		case APOLLO_PIXEL_TYPE_RGB16_565:
			return (apollo_color_t) {

				0,
					(uint8_t) ((color >> 11) & 0x1F),
					(uint8_t) ((color >> 5) & 0x3F),
					(uint8_t) ((color & 0x1F)),
					type
			};

		case APOLLO_PIXEL_TYPE_RGB16_555:
			return (apollo_color_t) {

				0,
					(uint8_t) ((color >> 10) & 0x1F),
					(uint8_t) ((color >> 5) & 0x1F),
					(uint8_t) ((color & 0x1F)),
					type
			};

		case APOLLO_PIXEL_TYPE_BGR16_565:
			return (apollo_color_t) {

				0,
					(uint8_t) ((color & 0x1F)),
					(uint8_t) ((color >> 5) & 0x3F),
					(uint8_t) ((color >> 11) & 0x1F),
					type
			};

		case APOLLO_PIXEL_TYPE_BGR16_555:
			return (apollo_color_t) {

				0,
					(uint8_t) ((color & 0x1F)),
					(uint8_t) ((color >> 5) & 0x1F),
					(uint8_t) ((color >> 10) & 0x1F),
					type
			};

		case APOLLO_PIXEL_TYPE_ARGB4444:
			return (apollo_color_t) {

				(uint8_t) ((color >> 12) & 0xF),
					(uint8_t) ((color >> 8) & 0xF),
					(uint8_t) ((color >> 4) & 0xF),
					(uint8_t) (color & 0xF),
					type
			};

		case APOLLO_PIXEL_TYPE_RGBA4444:
			return (apollo_color_t) {

				(uint8_t) (color & 0xF),
					(uint8_t) ((color >> 12) & 0xF),
					(uint8_t) ((color >> 8) & 0xF),
					(uint8_t) ((color >> 4) & 0xF),
					type
			};

		case APOLLO_PIXEL_TYPE_ABGR4444:
			return (apollo_color_t) {

				(uint8_t) ((color >> 12) & 0xF),
					(uint8_t) (color & 0xF),
					(uint8_t) ((color >> 4) & 0xF),
					(uint8_t) ((color >> 8) & 0xF),
					type
			};

		case APOLLO_PIXEL_TYPE_BGRA4444:
			return (apollo_color_t) {

				(uint8_t) (color & 0xF),
					(uint8_t) ((color >> 4) & 0xF),
					(uint8_t) ((color >> 8) & 0xF),
					(uint8_t) ((color >> 12) & 0xF),
					type
			};

		case APOLLO_PIXEL_TYPE_RGB332:
			return (apollo_color_t) {

				0,
					(uint8_t) ((color >> 5) & 0x7),
					(uint8_t) ((color >> 2) & 0x7),
					(uint8_t) (color & 0x3),
					type
			};

	}
	// This should be literally impossible.
	// clang-tidy still complains.
	return (apollo_color_t) { 0, 0, 0, 0, APOLLO_PIXEL_TYPE_UNKNOWN };
}