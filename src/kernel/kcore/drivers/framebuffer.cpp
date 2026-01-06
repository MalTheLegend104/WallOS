// #ifdef APOLLO_TEST

#include <stdio.h>

#include <apollo.h>
#include <drivers/serial.h>
#include <drivers/framebuffer.h>
#include <klibc/multiboot.h>

framebuffer_info_t internal_fb_info;
framebuffer_t fb;

void print_fb_info() {
	multiboot_tag_framebuffer* mb_fb = MultibootManager::getFramebufferTag();
	printf("Framebuffer Type:   %d\n", mb_fb->common.framebuffer_type);
	printf_serial("Framebuffer Type:   %d\r\n", mb_fb->common.framebuffer_type);

	printf("Framebuffer Addr:   0x%llx\n", mb_fb->common.framebuffer_addr);
	printf_serial("Framebuffer Addr:   0x%llx\r\n", mb_fb->common.framebuffer_addr);

	printf("Framebuffer pitch:  %lld\n", mb_fb->common.framebuffer_pitch);
	printf_serial("Framebuffer pitch:  %lld\r\n", mb_fb->common.framebuffer_pitch);

	printf("Framebuffer width:  %lld\n", mb_fb->common.framebuffer_width);
	printf_serial("Framebuffer width:  %lld\r\n", mb_fb->common.framebuffer_width);

	printf("Framebuffer height: %lld\n", mb_fb->common.framebuffer_height);
	printf_serial("Framebuffer height: %lld\r\n", mb_fb->common.framebuffer_height);

	printf("Framebuffer bpp:    %lld\n", mb_fb->common.framebuffer_bpp);
	printf_serial("Framebuffer bpp:    %lld\r\n", mb_fb->common.framebuffer_bpp);

	if (mb_fb->common.framebuffer_type == 1) {
		printf_serial("Framebuffer red mask:    %d\r\n", mb_fb->framebuffer_red_mask_size);
		printf_serial("Framebuffer red field:   %d\r\n", mb_fb->framebuffer_red_field_position);

		printf_serial("Framebuffer blue mask:   %d\r\n", mb_fb->framebuffer_blue_mask_size);
		printf_serial("Framebuffer blue field:  %d\r\n", mb_fb->framebuffer_blue_field_position);

		printf_serial("Framebuffer green mask:  %d\r\n", mb_fb->framebuffer_green_mask_size);
		printf_serial("Framebuffer green field: %d\r\n", mb_fb->framebuffer_green_field_position);
	}
}

static apollo_pixel_type
apollo_pixel_type_from_multiboot(const struct multiboot_tag_framebuffer* fb) {
	if (!fb)
		return APOLLO_PIXEL_TYPE_UNKNOWN;

	if (fb->common.framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB)
		return APOLLO_PIXEL_TYPE_UNKNOWN;

	uint8_t bpp = fb->common.framebuffer_bpp;

	uint8_t rpos = fb->framebuffer_red_field_position;
	uint8_t rsize = fb->framebuffer_red_mask_size;

	uint8_t gpos = fb->framebuffer_green_field_position;
	uint8_t gsize = fb->framebuffer_green_mask_size;

	uint8_t bpos = fb->framebuffer_blue_field_position;
	uint8_t bsize = fb->framebuffer_blue_mask_size;

#define MATCH(rp, rs, gp, gs, bp, bs) \
    (rpos == (rp) && rsize == (rs) && \
     gpos == (gp) && gsize == (gs) && \
     bpos == (bp) && bsize == (bs))

	/* =======================
	 * 32-bit formats
	 * ======================= */
	if (bpp == 32 && rsize == 8 && gsize == 8 && bsize == 8) {
		if (MATCH(16, 8, 8, 8, 0, 8))
			return APOLLO_PIXEL_TYPE_ARGB8888;

		if (MATCH(24, 8, 16, 8, 8, 8))
			return APOLLO_PIXEL_TYPE_RGBA32;

		if (MATCH(8, 8, 16, 8, 24, 8))
			return APOLLO_PIXEL_TYPE_BGRA32;

		if (MATCH(0, 8, 8, 8, 16, 8))
			return APOLLO_PIXEL_TYPE_BGR32;
	}

	/* =======================
	 * 24-bit formats
	 * ======================= */
	if (bpp == 24 && rsize == 8 && gsize == 8 && bsize == 8) {
		if (MATCH(16, 8, 8, 8, 0, 8))
			return APOLLO_PIXEL_TYPE_RGB24;

		if (MATCH(0, 8, 8, 8, 16, 8))
			return APOLLO_PIXEL_TYPE_BGR24;
	}

	/* =======================
	 * 16-bit formats
	 * ======================= */

	/* RGB565 / BGR565 */
	if (bpp == 16) {
		if (MATCH(11, 5, 5, 6, 0, 5))
			return APOLLO_PIXEL_TYPE_RGB16_565;

		if (MATCH(0, 5, 5, 6, 11, 5))
			return APOLLO_PIXEL_TYPE_BGR16_565;
	}

	/* RGB555 / BGR555 */
	if (bpp == 16) {
		if (MATCH(10, 5, 5, 5, 0, 5))
			return APOLLO_PIXEL_TYPE_RGB16_555;

		if (MATCH(0, 5, 5, 5, 10, 5))
			return APOLLO_PIXEL_TYPE_BGR16_555;
	}

	/* 4444 formats */
	if (bpp == 16 && rsize == 4 && gsize == 4 && bsize == 4) {
		if (MATCH(8, 4, 4, 4, 0, 4))
			return APOLLO_PIXEL_TYPE_ARGB4444;

		if (MATCH(12, 4, 8, 4, 4, 4))
			return APOLLO_PIXEL_TYPE_RGBA4444;

		if (MATCH(0, 4, 4, 4, 8, 4))
			return APOLLO_PIXEL_TYPE_ABGR4444;

		if (MATCH(4, 4, 8, 4, 12, 4))
			return APOLLO_PIXEL_TYPE_BGRA4444;
	}

	/* =======================
	 * 8-bit format
	 * ======================= */
	if (bpp == 8) {
		if (MATCH(5, 3, 2, 3, 0, 2))
			return APOLLO_PIXEL_TYPE_RGB332;
	}

#undef MATCH

	return APOLLO_PIXEL_TYPE_UNKNOWN;
}

void apollo_get_info(framebuffer_info_t* fb_info) {
	fb_info->width = internal_fb_info.width;
	fb_info->height = internal_fb_info.height;
	fb_info->pitch = internal_fb_info.pitch;
	fb_info->pixel_width = internal_fb_info.pixel_width;
	fb_info->type = apollo_pixel_type_from_multiboot(MultibootManager::getFramebufferTag());
}

void framebuffer_init() {
	multiboot_tag_framebuffer* e = MultibootManager::getFramebufferTag();

	internal_fb_info.width = (int) e->common.framebuffer_width;
	internal_fb_info.height = (int) e->common.framebuffer_height;
	internal_fb_info.pitch = (int) e->common.framebuffer_pitch;
	internal_fb_info.pixel_width = e->common.framebuffer_bpp / 8;

	// TODO: actually figure out the pixel type
	internal_fb_info.type = apollo_pixel_type_from_multiboot(e);
	printf_serial("[FB] framebuffer pixel type: %d", internal_fb_info.type);

	fb.buffer = (uint8_t*) e->common.framebuffer_addr;
}

#include <string.h>

void apollo_draw_buffer(framebuffer_t* buffer) {
	printf_serial("Framebuffer DEST: %p\r\nFramebuffer SRC:  %p\r\n", fb.buffer, buffer->buffer);

	memcpy(fb.buffer, buffer->buffer, fb.info->height * fb.info->width * fb.info->pixel_width);
}

// #endif