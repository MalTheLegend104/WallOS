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
		char signature[8];
		uint8_t checksum;
		char OEMID[6];
		uint8_t revision;
		uint32_t rsdtAddress;
	} __attribute__((packed));

	struct XSDP_t {
		char signature[8];
		uint8_t checksum;
		char OEMID[6];
		uint8_t revision;
		uint32_t rsdtAddress;      // deprecated since version 2.0

		uint32_t length;
		uint64_t xsdtAddress;
		uint8_t extendedChecksum;
		uint8_t reserved[3];
	} __attribute__((packed));

	typedef union {
		RSDP_t* rsdp;
		XSDP_t* xsdp;
	} acpi_tag;
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
	static acpi_tag* acpi;
	static multiboot_tag_framebuffer* framebuffer_tag;
	static void loadTags();
public:
	static multiboot_tag_mmap* getMMap() { return mmap; }
	static multiboot_tag_framebuffer* getFramebufferTag() { return framebuffer_tag; }
	static multiboot_info* getMultibootInfo() { return mbt_info; }
	static multiboot_header* getMultibootHeader() { return header; }
	static acpi_tag* getACPI() { return acpi; }
	static void initialize(uint32_t m, multiboot_info* info);
	static bool validateHeader();
	static bool validateMagic();
	static bool validateInfo();
	static bool validateAll();
};

multiboot_tag_bootdev* getBootDev();

#endif