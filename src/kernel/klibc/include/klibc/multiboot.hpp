#ifndef MULTIBOOT_HPP
#define MULTIBOOT_HPP
#include <stdint.h>
#include <multiboot.h>
#include <stdbool.h>

extern "C" {
	extern struct multiboot_header header_start;

	struct multiboot_info {
		uint32_t total_size;
		uint32_t reserved;
		struct multiboot_tag tags[0];
	};
}



/**
 * @brief Manages the multiboot information provided by grub and other Multiboot2 compliant bootloaders.
 */
class MultibootManager {
private:
	static uint32_t magic;
	static multiboot_info* mbt_info;
	static multiboot_header* header;
	static multiboot_tag_mmap* mmap;
	static multiboot_tag_framebuffer* framebuffer_tag;
	static void loadTags();
public:
	static multiboot_tag_mmap* getMMap() { return mmap; }
	static multiboot_tag_framebuffer* getFramebufferTag() { return framebuffer_tag; }
	static void initialize(uint32_t m, multiboot_info* info);
	static bool validateHeader();
	static bool validateMagic();
	static bool validateInfo();
	static bool validateAll();
};

#endif