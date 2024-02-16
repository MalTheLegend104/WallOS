#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <multiboot.h>

// #pragma GCC diagnostic ignored "-Wunused-parameter" 
// int framebuf(int argc, char** argv) {
// 	multiboot_tag_framebuffer* fb = MultibootManager::getFramebufferTag();
// 	printf("Framebuffer Type:   %d\n", fb->common.framebuffer_type);
// 	printf_serial("Framebuffer Type:   %d\r\n", fb->common.framebuffer_type);

// 	printf("Framebuffer Addr:   0x%llx\n", fb->common.framebuffer_addr);
// 	printf_serial("Framebuffer Addr:   0x%llx\r\n", fb->common.framebuffer_addr);

// 	printf("Framebuffer pitch:  %lld\n", fb->common.framebuffer_pitch);
// 	printf_serial("Framebuffer pitch:  %lld\r\n", fb->common.framebuffer_pitch);

// 	printf("Framebuffer width:  %lld\n", fb->common.framebuffer_width);
// 	printf_serial("Framebuffer width:  %lld\r\n", fb->common.framebuffer_width);

// 	printf("Framebuffer height: %lld\n", fb->common.framebuffer_height);
// 	printf_serial("Framebuffer height: %lld\r\n", fb->common.framebuffer_height);

// 	printf("Framebuffer bpp:    %lld\n", fb->common.framebuffer_bpp);
// 	printf_serial("Framebuffer bpp:    %lld\r\n", fb->common.framebuffer_bpp);

// 	return 0;
// }

static void putpixel(uint8_t* screen, int x, int y, int color, int pixelWidth, int pitch) {
	uint32_t where = (x * pixelWidth) + (y * pitch);
	screen[where] = color;              	  // BLUE
	screen[where + 1] = (color >> 8) & 255;   // GREEN
	screen[where + 2] = (color >> 16) & 255;  // RED
}
uint8_t pixelwidth = 0;
uint32_t pitch = 0;
static void fillrect(unsigned char* vram, unsigned char r, unsigned char g, unsigned char b, unsigned char w, unsigned char h) {
	unsigned char* where = vram;
	int i, j;

	for (i = 0; i < w; i++) {
		for (j = 0; j < h; j++) {
			//putpixel(vram, 64 + j, 64 + i, (r << 16) + (g << 8) + b);
			where[j * pixelwidth] = r;
			where[j * pixelwidth + 1] = g;
			where[j * pixelwidth + 2] = b;
		}
		where += pitch;
	}
}

#endif