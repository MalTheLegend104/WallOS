#ifndef MULTIBOOT_HPP
#define MULTIBOOT_HPP
#include <stdint.h>
#include <multiboot.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
	extern struct multiboot_header header_start;

	typedef struct {
		uint32_t total_size;
		uint32_t reserved;
		struct multiboot_tag tags[0];
	} multiboot_info;

	typedef struct {
		char signature[8];
		uint8_t checksum;
		char OEMID[6];
		uint8_t revision;
		uint32_t rsdtAddress;
	} __attribute__((packed)) RSDP_t;

	typedef struct {
		char signature[8];
		uint8_t checksum;
		char OEMID[6];
		uint8_t revision;
		uint32_t rsdtAddress;      // deprecated since version 2.0

		uint32_t length;
		uint64_t xsdtAddress;
		uint8_t extendedChecksum;
		uint8_t reserved[3];
	} __attribute__((packed)) XSDP_t;

	typedef union {
		RSDP_t* rsdp;
		XSDP_t* xsdp;
	} acpi_tag;
#ifdef __cplusplus
}
#endif

/**
 * @brief Manages the multiboot information provided by grub and other Multiboot2 compliant bootloaders.
 */
#ifdef __cplusplus

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


extern "C" {
#endif

	void* getAcpiRoot();

#ifdef __cplusplus
}
#endif

#endif // MULTIBOOT_HPP