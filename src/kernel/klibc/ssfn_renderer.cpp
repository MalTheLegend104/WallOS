#include <ssfn-renderer.h>
#include <klibc/multiboot.hpp>
#include <string.h>
//#include <font.h>
#define SSFN_CONSOLEBITMAP_TRUECOLOR        /* use the special renderer for 32 bit truecolor packed pixels */
#define SSFN_CONSOLEBITMAP_CONTROL
#include <ssfn.h>
extern char _binary_font_sfn_start;

extern "C" {
	void init_ssfn() {
		multiboot_tag_framebuffer* fb = MultibootManager::getFramebufferTag();
		ssfn_src = (ssfn_font_t*) &_binary_font_sfn_start;      /* the bitmap font to use */

		ssfn_dst.ptr = (uint8_t*) fb->common.framebuffer_addr;  /* address of the linear frame buffer */
		ssfn_dst.w = fb->common.framebuffer_width;   /* width */
		ssfn_dst.h = fb->common.framebuffer_height;  /* height */
		ssfn_dst.p = fb->common.framebuffer_pitch;   /* bytes per line */
		ssfn_dst.x = ssfn_dst.y = 0;                /* pen position */
		ssfn_dst.fg = 0xFFFFFF;                     /* foreground color */
	}

	int printf_ssfn(const char* str) {
		for (size_t i = 0; i < strlen(str); i++) {
			ssfn_putc(str[i]);
		}
		return 0;
	}

	int print_str(char* str) {
		char* string = str;
		for (size_t i = 0; i < strlen(str); i++) {
			ssfn_putc(ssfn_utf8(&string));
		}
	}

	int print_logo_ssfn() {
		char* a = "██╗    ██╗ █████╗ ██╗     ██╗      ██████╗ ███████╗\n\
██║    ██║██╔══██╗██║     ██║     ██╔═══██╗██╔════╝\n\
██║ █╗ ██║███████║██║     ██║     ██║   ██║███████╗\n\
██║███╗██║██╔══██║██║     ██║     ██║   ██║╚════██║\n\
╚███╔███╔╝██║  ██║███████╗███████╗╚██████╔╝███████║\n\
 ╚══╝╚══╝ ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝ ╚══════╝";
		ssfn_dst.fg = 0x2BE3FF;
		print_str(a);
		ssfn_dst.fg = 0xFFFFFF;
	}
}