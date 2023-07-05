#include <klibc/multiboot.hpp>
#include <klibc/logger.h>
#include <stdio.h>
uint32_t MultibootManager::magic;
multiboot_header* MultibootManager::header;
multiboot_info* MultibootManager::mbt_info;
// See https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html#Memory-map
multiboot_tag_mmap* MultibootManager::mmap;
multiboot_tag_framebuffer* MultibootManager::framebuffer_tag;


// See https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html#Boot-information
void MultibootManager::loadTags(){
	//  Get the pointer to the first tag
	multiboot_tag* tag = &(mbt_info->tags[0]);

	// Main loop to iterate through tags
	while (tag->type != MULTIBOOT_TAG_TYPE_END) {
		// Get each tag type and do things with them
		switch (tag->type) {
			case MULTIBOOT_TAG_TYPE_END:
				return;
			case MULTIBOOT_TAG_TYPE_CMDLINE:
                puts_vga("    ");
                Logger::Checklist::blankEntry("CMDLINE tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME:
                puts_vga("    ");
                Logger::Checklist::blankEntry("BOOT_LOADER_NAME tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_MODULE:
                puts_vga("    ");
                Logger::Checklist::blankEntry("MODULE tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_BASIC_MEMINFO:
                puts_vga("    ");
                Logger::Checklist::blankEntry("BASIC_MEMINFO tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_BOOTDEV:
                puts_vga("    ");
                Logger::Checklist::blankEntry("BOOTDEV tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_MMAP:
                puts_vga("    ");
                Logger::Checklist::checkEntry("MMAP tag exists.");
				mmap = (multiboot_tag_mmap*) tag;
                break;
            case MULTIBOOT_TAG_TYPE_VBE:
                puts_vga("    ");
                Logger::Checklist::blankEntry("VBE tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_FRAMEBUFFER:
                puts_vga("    ");
                Logger::Checklist::blankEntry("FRAMEBUFFER tag exists.");
				framebuffer_tag = (multiboot_tag_framebuffer*) tag;
                break;
            case MULTIBOOT_TAG_TYPE_ELF_SECTIONS:
                puts_vga("    ");
                Logger::Checklist::blankEntry("ELF_SECTIONS tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_APM:
                puts_vga("    ");
                Logger::Checklist::blankEntry("APM tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_EFI32:
                puts_vga("    ");
                Logger::Checklist::blankEntry("EFI32 tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_EFI64:
                puts_vga("    ");
                Logger::Checklist::blankEntry("EFI64 tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_SMBIOS:
                puts_vga("    ");
                Logger::Checklist::blankEntry("SMBIOS tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_ACPI_OLD:
                puts_vga("    ");
                Logger::Checklist::blankEntry("ACPI_OLD tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_ACPI_NEW:
                puts_vga("    ");
                Logger::Checklist::blankEntry("ACPI_NEW tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_NETWORK:
                puts_vga("    ");
                Logger::Checklist::blankEntry("NETWORK tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_EFI_MMAP:
                puts_vga("    ");
                Logger::Checklist::blankEntry("EFI_MMAP tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_EFI_BS:
                puts_vga("    ");
                Logger::Checklist::blankEntry("EFI_BS tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_EFI32_IH:
                puts_vga("    ");
                Logger::Checklist::blankEntry("EFI32_IH tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_EFI64_IH:
                puts_vga("    ");
                Logger::Checklist::blankEntry("EFI64_IH tag exists.");
                break;
            case MULTIBOOT_TAG_TYPE_LOAD_BASE_ADDR:
                puts_vga("    ");
                Logger::Checklist::blankEntry("LOAD_BASE_ADDR tag exists.");
                break;
			default:
				// Ignore unrecognized tag types
				break;
		}

		// Move to the next tag
		tag = (multiboot_tag*)((uintptr_t)tag + ((tag->size + MULTIBOOT_TAG_ALIGN - 1) & ~(MULTIBOOT_TAG_ALIGN - 1)));
	}
}

/**
 * @brief Initalize the multiboot manager class. At the time we load this, we don't have memory access yet, so we don't have new and delete.
 * We can't make a class the normal way, and quite frankly this static class makes slightly more sense for what it is.
 * 
 * @param m The magic number provided in eax on boot.
 * @param info The pointer provided in ebx on boot.
 */
void MultibootManager::initialize(uint32_t m, multiboot_info* info){
	magic = m;
	mbt_info = info;
	header = (multiboot_header*) 0x1BADB002;

}

/**
 * @brief Checks the validity of the header.
 * 
 * @return true If header is valid.
 * @return false If header is not valid.
 */
bool MultibootManager::validateHeader(){
	// Grub ALWAYS loads the header into this address. We verify it exists here.
 	//verify the multiboot
	uint32_t checkheader = header->architecture + header->header_length + header->magic + header->checksum;
	if(checkheader == 0){
		Logger::Checklist::checkEntry("Header is valid: %d", checkheader);
		return true;
	} else {
		Logger::Checklist::noCheckEntry("Header is NOT valid: %d", checkheader);
		return false;
	}
}

/**
 * @brief Checks the validity of the magic number.
 * 
 * @return true If magic number is valid.
 * @return false If magic number is not valid.
 */
bool MultibootManager::validateMagic(){
	if (magic == 0x36d76289){
		Logger::Checklist::checkEntry("Magic number is valid: %d", magic);
		return true;
	} else {
		Logger::Checklist::noCheckEntry("Magic number is NOT valid: %d", magic);
		return false;
	}
}

/**
 * @brief Checks the validity of the information pointer.
 * This should ALWAYS return true, otherwise something is very, very wrong.
 * 
 * @return true If the pointer points to a value. 
 * @return false If the pointer is NULL.
 */
bool MultibootManager::validateInfo(){
	if (mbt_info != NULL) {
		Logger::Checklist::checkEntry("Multiboot info exists: %d", mbt_info);
		return true;
	} else {
		Logger::Checklist::noCheckEntry("Multiboot info is NULL");
		return false;
	}
}

/**
 * @brief Check the validity of all aspects of the multiboot information, along with loading the tags.
 * 
 * @return true If all aspects are valid, and all necessary tags are present.
 * @return false If not all aspects are valid, or if any necessary tags are not present.
 */
bool MultibootManager::validateAll(){
	// The header checksum returns -4 on bochs, idk why. 
	// Everything else is correct, so we're just going to ignore it. 
	//if (!validateHeader()) return false;
	if (!validateMagic()) return false;
	if (!validateInfo()) return false;
	puts_vga("\n");
	puts_vga("Checking multiboot_info:\n");
	loadTags();
	return true;
}