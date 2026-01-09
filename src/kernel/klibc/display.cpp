#include <klibc/display.h>
#include <klibc/kprint.h>
#include <apollo.h>
#include <string.h>

#include <drivers/serial.h>

#include <drivers/framebuffer.h>
#include <klibc/multiboot.h>

#include <fonts/apollo_12x18.h>
#include <fonts/apollo_8x16.h>
#include <fonts/apollo_16x16.h>
// ------------------------------------------------------------------------------------------------
// Internal State
// ------------------------------------------------------------------------------------------------
static display_mode_t display_mode = DISPLAY_MODE_VGA_TEXT;
static display_color_t current_fg = DISPLAY_DEFAULT_FG;
static display_color_t current_bg = DISPLAY_DEFAULT_BG;

// Framebuffer mode state
static framebuffer_t front_buffer;
static framebuffer_t back_buffer;

// This is to solve a chicken-and-egg scenario.
// We need to render to the back buffer for performance.
// We can't get a back buffer until we have the allocator.
// This stores the current framebuffer target.
static framebuffer_t* render_target = &front_buffer;
static bool use_backbuffer = false;

static framebuffer_info_t fb_info;
static apollo_font_instance font_instance = { NULL, 1, 1 };
static int fb_cursor_x = 0;
static int fb_cursor_y = 0;

// ------------------------------------------------------------------------------------------------
// Color Conversion
// ------------------------------------------------------------------------------------------------
static apollo_color_t display_color_to_apollo(display_color_t color) {
	apollo_color_t result = { .alpha = 255, .red = 0, .green = 0, .blue = 0, .type = APOLLO_PIXEL_TYPE_ARGB8888 };

	switch (color) {
		case DISPLAY_COLOR_BLACK:       result.alpha = 0; result.red = 0;   result.green = 0;   result.blue = 0;   break;
		case DISPLAY_COLOR_BLUE:        result.red = 0;   result.green = 0;   result.blue = 170; break;
		case DISPLAY_COLOR_GREEN:       result.red = 0;   result.green = 170; result.blue = 0;   break;
		case DISPLAY_COLOR_CYAN:        result.red = 0;   result.green = 170; result.blue = 170; break;
		case DISPLAY_COLOR_RED:         result.red = 170; result.green = 0;   result.blue = 0;   break;
		case DISPLAY_COLOR_PURPLE:      result.red = 170; result.green = 0;   result.blue = 170; break;
		case DISPLAY_COLOR_BROWN:       result.red = 170; result.green = 85;  result.blue = 0;   break;
		case DISPLAY_COLOR_LIGHT_GREY:  result.red = 170; result.green = 170; result.blue = 170; break;
		case DISPLAY_COLOR_DARK_GREY:   result.red = 85;  result.green = 85;  result.blue = 85;  break;
		case DISPLAY_COLOR_LIGHT_BLUE:  result.red = 85;  result.green = 85;  result.blue = 255; break;
		case DISPLAY_COLOR_LIGHT_GREEN: result.red = 85;  result.green = 255; result.blue = 85;  break;
		case DISPLAY_COLOR_LIGHT_CYAN:  result.red = 85;  result.green = 255; result.blue = 255; break;
		case DISPLAY_COLOR_LIGHT_RED:   result.red = 255; result.green = 85;  result.blue = 85;  break;
		case DISPLAY_COLOR_PINK:        result.red = 255; result.green = 85;  result.blue = 255; break;
		case DISPLAY_COLOR_YELLOW:      result.red = 255; result.green = 255; result.blue = 85;  break;
		case DISPLAY_COLOR_WHITE:       result.red = 255; result.green = 255; result.blue = 255; break;
		// case DISPLAY_DEFAULT_BG:        result.red = 0; result.green = 0; result.blue = 0;  break;
		// case DISPLAY_DEFAULT_FG:        result.red = 255; result.green = 255; result.blue = 255; break;
		default:                        result.red = 170; result.green = 170; result.blue = 170; break;
	}

	return result;
}


/* Optimized version of memmove for large regions
 *
 * This function copies `n` bytes from `src` to `dest` using the largest
 * aligned transfers possible. It is designed specifically for large regions,
 * where reads and writes to memory are often significantly slower than
 * normal RAM accesses.
 *
 * How it works:
 * 1. Aligns the destination to an 8-byte boundary, copying bytes as necessary to get there.
 * 2. Bulk copy in 64 bit chunks
 * 3. Any remaining bytes are copied one by one
 *
 * Why it's better:
 * - My regular memmove is as basic as can be. There is *zero* optimization. It copies everything byte by byte.
 * - This doesn't particularly check things like it should to be a libc compliant memmove
 * - This function assumes that `dest` and `src` do not overlap.
 *     - If overlap is possible, behavior is undefined, unlike `memmove`.
 *     - This is definitely used in contexts where they overlap, like in the display framebuffer.
 *       It is fine to overlap, if you can ensure this won't try to read and write to the same chunk at the same time.
 */
static inline void fast_memmove(void* dest, const void* src, size_t n) {
	uint8_t* dest8 = (uint8_t*) dest;
	const uint8_t* src8 = (const uint8_t*) src;

	// Align to 8-byte boundary
	while (n && ((uintptr_t) dest8 & 7)) {
		*dest8++ = *src8++;
		n--;
	}

	// Copy 8 bytes at a time
	uint64_t* d64 = (uint64_t*) dest8;
	const uint64_t* s64 = (const uint64_t*) src8;

	// Do 64 bit chunks while we can
	while (n >= 64) {
		d64[0] = s64[0];
		d64[1] = s64[1];
		d64[2] = s64[2];
		d64[3] = s64[3];
		d64[4] = s64[4];
		d64[5] = s64[5];
		d64[6] = s64[6];
		d64[7] = s64[7];
		d64 += 8;
		s64 += 8;
		n -= 64;
	}

	while (n >= 8) {
		*d64++ = *s64++;
		n -= 8;
	}

	// Copy remaining bytes
	dest8 = (uint8_t*) d64;
	src8 = (const uint8_t*) s64;
	while (n--) {
		*dest8++ = *src8++;
	}
}

// ------------------------------------------------------------------------------------------------
// Framebuffer Mode Helpers
// ------------------------------------------------------------------------------------------------
void display_flush() {
	if (display_mode != DISPLAY_MODE_FRAMEBUFFER) return;
	if (!use_backbuffer) return;

	// Direct 64-bit copy (optimized by the Write-Combining PAT you set up)
	uint64_t* src = (uint64_t*) back_buffer.buffer;
	uint64_t* dest = (uint64_t*) front_buffer.buffer;
	size_t count = (fb_info.height * fb_info.pitch) / 8;

	for (size_t i = 0; i < count; i++) {
		dest[i] = src[i];
	}

	asm volatile("sfence" ::: "memory");
}

static void fb_scroll(void) {
	if (!font_instance.font) return;

	int font_height = font_instance.font->font_height * font_instance.y_scaling;
	size_t pitch = fb_info.pitch;

	// Use the CURRENT render target (VRAM early, RAM late)
	fast_memmove(render_target->buffer,
		render_target->buffer + (pitch * font_height),
		pitch * (fb_info.height - font_height));

	// Clear bottom line
	apollo_color_t bg = display_color_to_apollo(current_bg);
	coordinate_pair bottom_left = { 0, fb_info.height - font_height };
	apollo_set_rect(render_target, bottom_left, PAIR_TOP_LEFT,
		fb_info.width, font_height, bg);

	if (use_backbuffer) {
		display_flush();
	}
}

static void fb_putc_internal(uint8_t c) {
	if (!font_instance.font) return;

	int char_width = font_instance.font->font_width * font_instance.x_scaling;
	int char_height = font_instance.font->font_height * font_instance.y_scaling;
	int chars_per_line = fb_info.width / char_width;
	int max_lines = fb_info.height / char_height;

	apollo_font_color_t colors;
	colors.foreground = display_color_to_apollo(current_fg);
	colors.background = display_color_to_apollo(current_bg);

	if (c == '\0') return;

	if (c == '\n') {
		fb_cursor_x = 0;
		fb_cursor_y++;
		if (fb_cursor_y >= max_lines) {
			fb_scroll();
			fb_cursor_y = max_lines - 1;
		}
		return;
	}

	if (c == '\r') {
		fb_cursor_x = 0;
		return;
	}

	if (c == '\b') {
		if (fb_cursor_x > 0) {
			fb_cursor_x--;
			coordinate_pair pos = {
				fb_cursor_x * char_width,
				fb_cursor_y * char_height
			};
			apollo_set_rect(render_target, pos, PAIR_TOP_LEFT,
				char_width, char_height,
				colors.background);
		}
		return;
	}

	if (c == '\t') {
		fb_cursor_x += 3;
		if (fb_cursor_x >= chars_per_line) {
			fb_cursor_x = 0;
			fb_cursor_y++;
			if (fb_cursor_y >= max_lines) {
				fb_scroll();
				fb_cursor_y = max_lines - 1;
			}
		}
		return;
	}

	coordinate_pair pos = {
		fb_cursor_x * char_width,
		fb_cursor_y * char_height
	};
	apollo_print_char(render_target, &font_instance, c, pos, colors);

	fb_cursor_x++;
	if (fb_cursor_x >= chars_per_line) {
		fb_cursor_x = 0;
		fb_cursor_y++;
		if (fb_cursor_y >= max_lines) {
			fb_scroll();
			fb_cursor_y = max_lines - 1;
		}
	}
}


void print_fb_info() {
	multiboot_tag_framebuffer* mb_fb = MultibootManager::getFramebufferTag();
	printf_serial("[FB] Multiboot Framebuffer Info:\r\n");
	printf_serial("\tFramebuffer Type:   %d\r\n", mb_fb->common.framebuffer_type);
	printf_serial("\tFramebuffer Addr:   0x%llx\r\n", mb_fb->common.framebuffer_addr);
	printf_serial("\tFramebuffer pitch:  %lld\r\n", mb_fb->common.framebuffer_pitch);
	printf_serial("\tFramebuffer width:  %lld\r\n", mb_fb->common.framebuffer_width);
	printf_serial("\tFramebuffer height: %lld\r\n", mb_fb->common.framebuffer_height);
	printf_serial("\tFramebuffer bpp:    %lld\r\n", mb_fb->common.framebuffer_bpp);

	if (mb_fb->common.framebuffer_type == 1) {
		printf_serial("\tFramebuffer red mask:    %d\r\n", mb_fb->framebuffer_red_mask_size);
		printf_serial("\tFramebuffer red field:   %d\r\n", mb_fb->framebuffer_red_field_position);

		printf_serial("\tFramebuffer blue mask:   %d\r\n", mb_fb->framebuffer_blue_mask_size);
		printf_serial("\tFramebuffer blue field:  %d\r\n", mb_fb->framebuffer_blue_field_position);

		printf_serial("\tFramebuffer green mask:  %d\r\n", mb_fb->framebuffer_green_mask_size);
		printf_serial("\tFramebuffer green field: %d\r\n", mb_fb->framebuffer_green_field_position);
	}
}

static apollo_pixel_type apollo_pixel_type_from_multiboot(const struct multiboot_tag_framebuffer* fb) {
	if (!fb) return APOLLO_PIXEL_TYPE_UNKNOWN;

	if (fb->common.framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB) return APOLLO_PIXEL_TYPE_UNKNOWN;

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

#include <string.h>

void apollo_draw_buffer(framebuffer_t* buffer) {
	printf_serial("Framebuffer DEST: %p\r\nFramebuffer SRC:  %p\r\n", front_buffer.buffer, buffer->buffer);

	memcpy(front_buffer.buffer, buffer->buffer, front_buffer.info->height * front_buffer.info->width * front_buffer.info->pixel_width);
}

void apollo_get_info(framebuffer_info_t* fb_info) {
	fb_info->width = front_buffer.info->width;
	fb_info->height = front_buffer.info->height;
	fb_info->pitch = front_buffer.info->pitch;
	fb_info->pixel_width = front_buffer.info->pixel_width;
	fb_info->type = front_buffer.info->type;
}

void init_framebuffer(framebuffer_t* framebuffer) {
	multiboot_tag_framebuffer* e = MultibootManager::getFramebufferTag();

	framebuffer->info->width = (int) e->common.framebuffer_width;
	framebuffer->info->height = (int) e->common.framebuffer_height;
	framebuffer->info->pitch = (int) e->common.framebuffer_pitch;
	framebuffer->info->pixel_width = e->common.framebuffer_bpp / 8;

	framebuffer->info->type = apollo_pixel_type_from_multiboot(e);
	// printf_serial("[FB] framebuffer pixel type: %d", internal_fb_info.type);

	framebuffer->buffer = (uint8_t*) e->common.framebuffer_addr;

	// This is the default
	// I think I'm going to only have 3 included options, and use one based on the framebuffer size.
	// 8x16, 12x18, 16x32
	// We have some "special" square fonts, 8x8 and 16x16.
	// I will make an interface for changing fonts & scaling
	font_instance.font = &apollo_12x18;
	font_instance.x_scaling = font_instance.y_scaling = 1;
}

const char* pixel_type_to_string(apollo_pixel_type type) {
	switch (type) {
		case APOLLO_PIXEL_TYPE_UNKNOWN:		return "Unknown Pixel Type";
		case APOLLO_PIXEL_TYPE_ARGB8888: 	return "ARGB8888";
		case APOLLO_PIXEL_TYPE_RGB32: 		return "RGB 32-bit";
		case APOLLO_PIXEL_TYPE_RGBA32: 		return "RGBA 32-bit";
		case APOLLO_PIXEL_TYPE_BGR32: 		return "BGR 32-bit";
		case APOLLO_PIXEL_TYPE_BGRA32: 		return "BGRA 32-bit";
		case APOLLO_PIXEL_TYPE_RGB16_565: 	return "RGB 16-bit (5-6-5)";
		case APOLLO_PIXEL_TYPE_RGB16_555: 	return "RGB 16-bit (5-5-5)";
		case APOLLO_PIXEL_TYPE_BGR16_565: 	return "BGR 16-bit (5-6-5)";
		case APOLLO_PIXEL_TYPE_BGR16_555: 	return "BGR 16-bit (5-5-5)";
		case APOLLO_PIXEL_TYPE_RGB24: 		return "RGB 24-bit";
		case APOLLO_PIXEL_TYPE_BGR24: 		return "BGR 24-bit";
		default: 							return "Invalid Pixel Type";
	}
}
// ------------------------------------------------------------------------------------------------
// Public API Implementation
// ------------------------------------------------------------------------------------------------

void print_apollo_info() {
	if (front_buffer.info == NULL) {
		printf_serial("[FB] Framebuffer isn't initalized yet...\r\n");
		return;
	}

	printf_serial("[FB] Apollo Info:\r\n");
	printf_serial("\tWidth:  %d\r\n", front_buffer.info->width);
	printf_serial("\tHeight: %d\r\n", front_buffer.info->height);
	printf_serial("\tPitch:  %d\r\n", front_buffer.info->pitch);
	printf_serial("\tPixel Width: %d\r\n", front_buffer.info->pixel_width);
	printf_serial("\tPixel Type: %s\r\n", pixel_type_to_string(front_buffer.info->type));
}

bool display_init(display_mode_t mode) {
	display_mode = mode;

	if (mode == DISPLAY_MODE_VGA_TEXT) {
		initScreen();
		return true;
	} else if (mode == DISPLAY_MODE_FRAMEBUFFER) {
		front_buffer.info = &fb_info;

		init_framebuffer(&front_buffer);

		fb_cursor_x = 0;
		fb_cursor_y = 0;

		// font_instance.font = &apollo_12x18;
		// font_instance.x_scaling = 1;
		// font_instance.y_scaling = 1;

		// These should already be set, but we want to ensure they are so we don't end up causing a ton of problems.
		render_target = &front_buffer;
		use_backbuffer = false;

		if (front_buffer.buffer && font_instance.font) {
			print_fb_info();
			print_apollo_info();

			apollo_color_t bg = display_color_to_apollo(DISPLAY_COLOR_BLACK);
			apollo_fill_buffer(&front_buffer, bg);
			return true;
		}
		return false;
	}

	return false;
}

#include <memory/kernel_alloc.h>

void display_init_late() {
	if (display_mode == DISPLAY_MODE_VGA_TEXT) return;

	size_t fb_size = fb_info.height * fb_info.pitch;

	// 1. Map or Allocate the system RAM buffer
	// You might use your own 'kmalloc' or 'PageFrameAllocator::requestPage'
	void* ram_buffer = kalloc(fb_size);

	if (ram_buffer) {
		back_buffer.info = &fb_info;
		back_buffer.buffer = (uint8_t*) ram_buffer;

		// 2. Sync current screen state to the backbuffer so we don't start with a black screen
		memcpy(back_buffer.buffer, front_buffer.buffer, fb_size);

		// 3. SWITCH THE TARGET: All future draws go to RAM
		render_target = &back_buffer;
		use_backbuffer = true;

		printf_serial("[FB] Switched to back buffer.\r\n");
	}
}

display_mode_t display_get_mode(void) {
	return display_mode;
}

bool display_switch_mode(display_mode_t mode) {
	return display_init(mode);
}

void display_clear(void) {
	if (display_mode == DISPLAY_MODE_VGA_TEXT) {
		clearVGABuf();
		enable_cursor(0, 25);
		update_cursor(0, 0);
	} else {
		apollo_color_t bg = display_color_to_apollo(current_bg);

		apollo_fill_buffer(render_target, bg);

		fb_cursor_x = 0;
		fb_cursor_y = 0;

		if (use_backbuffer) {
			display_flush();
		}
	}
}

void display_clear_row(void) {
	if (display_mode == DISPLAY_MODE_VGA_TEXT) {
		clear_current_row();
	} else {
		if (!font_instance.font) return;
		apollo_color_t bg = display_color_to_apollo(current_bg);
		int char_height = font_instance.font->font_height * font_instance.y_scaling;
		coordinate_pair pos = { 0, fb_cursor_y * char_height };

		apollo_set_rect(render_target, pos, PAIR_TOP_LEFT, fb_info.width, char_height, bg);

		fb_cursor_x = 0;

		if (use_backbuffer) {
			display_flush();
		}
	}
}

void display_set_colors(int fg, int bg) {
	current_fg = (display_color_t) fg;
	current_bg = (display_color_t) bg;

	if (display_mode == DISPLAY_MODE_VGA_TEXT) {
		set_colors((display_color_t) fg, (display_color_t) bg);
	}
	// Framebuffer mode stores colors and applies them during rendering
}

void display_set_colors_default(void) {
	display_set_colors(DISPLAY_DEFAULT_FG, DISPLAY_DEFAULT_BG);
}

void display_putc(unsigned char c) {
	if (display_mode == DISPLAY_MODE_VGA_TEXT) {
		putc_vga(c);
	} else {
		fb_putc_internal(c);
	}
}

void display_putc_unfiltered(char c) {
	if (display_mode == DISPLAY_MODE_VGA_TEXT) {
		putc_vga_unfiltered(c);
	} else {
		fb_putc_internal(c);
	}
}

void display_puts(const char* str) {
	if (display_mode == DISPLAY_MODE_VGA_TEXT) {
		puts_vga(str);
	} else {
		for (const char* p = str; *p; p++) {
			fb_putc_internal(*p);
		}
		display_flush();
	}
}

void display_puts_color(const char* str, display_color_t fg, display_color_t bg) {
	if (display_mode == DISPLAY_MODE_VGA_TEXT) {
		puts_vga_color(str, fg, bg);
	} else {
		display_color_t old_fg = current_fg;
		display_color_t old_bg = current_bg;
		display_set_colors(fg, bg);
		display_puts(str);
		display_set_colors(old_fg, old_bg);
	}
}

int printf_color(int fg, int bg, const char* fmt, ...) {
	display_set_colors((display_color_t) fg, (display_color_t) bg);
	va_list arg;
	int ret;
	va_start(arg, fmt);
	ret = vprintf(fmt, arg);
	va_end(arg);
	display_set_colors_default();
	return ret;
}

int vprintf_color(int fg, int bg, const char* fmt, va_list arg) {
	display_set_colors((display_color_t) fg, (display_color_t) bg);

	int ret;
	ret = vprintf(fmt, arg);

	display_set_colors_default();
	return ret;
}

void display_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
	if (display_mode == DISPLAY_MODE_VGA_TEXT) {
		enable_cursor(cursor_start, cursor_end);
	}
	// Framebuffer mode: Could implement a software cursor if needed
}

void display_disable_cursor(void) {
	if (display_mode == DISPLAY_MODE_VGA_TEXT) {
		disable_cursor();
	}
}

void display_update_cursor(int x, int y) {
	if (display_mode == DISPLAY_MODE_VGA_TEXT) {
		update_cursor(x, y);
	} else {
		fb_cursor_x = x;
		fb_cursor_y = y;
	}
}

void display_get_cursor(int* x, int* y) {
	if (display_mode == DISPLAY_MODE_VGA_TEXT) {
		// You'll need to add this to kprint.h if it doesn't exist
		// For now, stub it
		if (x) *x = 0;
		if (y) *y = 0;
	} else {
		if (x) *x = fb_cursor_x;
		if (y) *y = fb_cursor_y;
	}
}

void display_get_dimensions(int* width, int* height) {
	if (display_mode == DISPLAY_MODE_VGA_TEXT) {
		if (width) *width = 80;
		if (height) *height = 25;
	} else {
		if (font_instance.font) {
			int char_width = font_instance.font->font_width * font_instance.x_scaling;
			int char_height = font_instance.font->font_height * font_instance.y_scaling;
			if (width) *width = fb_info.width / char_width;
			if (height) *height = fb_info.height / char_height;
		} else {
			if (width) *width = 0;
			if (height) *height = 0;
		}
	}
}

void display_set_font(const apollo_font* font) {
	font_instance.font = font;
}

void display_set_font_scale(uint8_t x_scale, uint8_t y_scale) {
	if (x_scale == 0) x_scale = 1;
	if (y_scale == 0) y_scale = 1;
	font_instance.x_scaling = x_scale;
	font_instance.y_scaling = y_scale;
}

const apollo_font_instance* display_get_font_instance(void) {
	if (display_mode == DISPLAY_MODE_FRAMEBUFFER) {
		return &font_instance;
	}
	return NULL;
}

#ifdef __is_kernel_
void display_panic(const char* error) {
	if (display_mode == DISPLAY_MODE_VGA_TEXT) {
		pink_screen(error);
	} else {
		use_backbuffer = false;
		// Implement framebuffer panic screen
		apollo_color_t pink = display_color_to_apollo(DISPLAY_COLOR_PINK);
		apollo_fill_buffer(&front_buffer, pink);

		fb_cursor_x = 0;
		fb_cursor_y = 0;
		display_set_colors(DISPLAY_COLOR_WHITE, DISPLAY_COLOR_PINK);
		display_puts(error);
	}
}

void display_panic_array(const char** errors, uint8_t length) {
	if (display_mode == DISPLAY_MODE_VGA_TEXT) {
		pink_screen_sa(errors, length);
	} else {
		use_backbuffer = false;
		apollo_color_t pink = display_color_to_apollo(DISPLAY_COLOR_PINK);
		apollo_fill_buffer(&front_buffer, pink);

		fb_cursor_x = 0;
		fb_cursor_y = 0;
		display_set_colors(DISPLAY_COLOR_WHITE, DISPLAY_COLOR_PINK);

		for (uint8_t i = 0; i < length; i++) {
			display_puts(errors[i]);
			display_putc('\n');
		}
	}
}
#endif // __is_kernel_