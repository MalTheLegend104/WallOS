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
	struct RSDP_t {
		char Signature[8];
		uint8_t Checksum;
		char OEMID[6];
		uint8_t Revision;
		uint32_t RsdtAddress;
	} __attribute__((packed));

	struct XSDP_t {
		char Signature[8];
		uint8_t Checksum;
		char OEMID[6];
		uint8_t Revision;
		uint32_t RsdtAddress;      // deprecated since version 2.0

		uint32_t Length;
		uint64_t XsdtAddress;
		uint8_t ExtendedChecksum;
		uint8_t reserved[3];
	} __attribute__((packed));
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
	static multiboot_tag_old_acpi* acpi_old;
	static multiboot_tag_new_acpi* acpi_new;
	static multiboot_tag_framebuffer* framebuffer_tag;
	static void loadTags();
public:
	static multiboot_tag_mmap* getMMap() { return mmap; }
	static multiboot_tag_framebuffer* getFramebufferTag() { return framebuffer_tag; }
	static multiboot_info* getMultibootInfo() { return mbt_info; }
	static multiboot_header* getMultibootHeader() { return header; }
	static multiboot_tag_old_acpi* getOldACPI() { return acpi_old; }
	static void initialize(uint32_t m, multiboot_info* info);
	static bool validateHeader();
	static bool validateMagic();
	static bool validateInfo();
	static bool validateAll();
};

#endif