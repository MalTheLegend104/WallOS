#ifdef APOLLO_TEST

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

void apollo_get_info(framebuffer_info_t* fb_info) {
	fb_info->width = internal_fb_info.width;
	fb_info->height = internal_fb_info.height;
	fb_info->pitch = internal_fb_info.pitch;
	fb_info->pixel_width = internal_fb_info.pixel_width;

	// TODO: actually figure out the pixel type
	if (fb_info->pixel_width == 4) {
		fb_info->type = APOLLO_PIXEL_TYPE_RGBA32;
	} else if (fb_info->pixel_width == 3) {
		fb_info->type = APOLLO_PIXEL_TYPE_RGB24;
	} else if (fb_info->pixel_width == 2) {
		fb_info->type = APOLLO_PIXEL_TYPE_RGB16_565;
	} else {
		fb_info->type = APOLLO_PIXEL_TYPE_RGB332;
	}
}

void framebuffer_init() {
	multiboot_tag_framebuffer* e = MultibootManager::getFramebufferTag();

	internal_fb_info.width = (int) e->common.framebuffer_width;
	internal_fb_info.height = (int) e->common.framebuffer_height;
	internal_fb_info.pitch = (int) e->common.framebuffer_pitch;
	internal_fb_info.pixel_width = e->common.framebuffer_bpp / 8;

	// TODO: actually figure out the pixel type
	if (internal_fb_info.pixel_width == 4) {
		internal_fb_info.type = APOLLO_PIXEL_TYPE_RGBA32;
	} else if (internal_fb_info.pixel_width == 3) {
		internal_fb_info.type = APOLLO_PIXEL_TYPE_RGB24;
	} else if (internal_fb_info.pixel_width == 2) {
		internal_fb_info.type = APOLLO_PIXEL_TYPE_RGB16_565;
	} else {
		internal_fb_info.type = APOLLO_PIXEL_TYPE_RGB332;
	}

	fb.buffer = (uint8_t*) e->common.framebuffer_addr;
}

#include <string.h>

void apollo_draw_buffer(framebuffer_t* buffer) {
	printf_serial("Framebuffer DEST: %p\r\nFramebuffer SRC:  %p\r\n", fb.buffer, buffer->buffer);

	memcpy(fb.buffer, buffer->buffer, fb.info->height * fb.info->width * fb.info->pixel_width);
}

#endif