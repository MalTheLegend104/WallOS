/* Read the AMD and Intel programmers manuals to get an understanding as to how page tables work. The osdev wiki does a bad job in my opinion.
 * The naming of things and the general structure are heavily inspired by the AMD manuals.
 *
 * Below is a summary of how the three most important addresses work in terms of paging:
 *
 * Virtual Addresses are the addresses given to a program by things like malloc() or new.
 * They are also given to the program as a whole when it is loaded into memory.
 * Virtual Addresses are laid out in a very particular way on x86_64:
 * |63            48|47     39|38     30|29     21|20     12|11         0|
 *  0000000000000000 000000000 000000000 000000000 000000000 000000000000
 * Meaning of Bits:
 * 63-48: Reserved, must ALL be the same type of bit (either 0 or 1)
 * 47-39: Offset in the top level page (pml4).
 * 38-30: Offset in the PDP that the pml4 pointed to.
 * 29-21: Offset in the PDE that the PDP pointed to.
 * 20-11: Offset in the PTE that the PDE pointed to.
 * 11-0:  Offset in the page. 4095 is the max, pointing to the very last byte of the page.
 * Important Note: This only applies to level 4 paging. Using different levels of paging results in slightly different addresses.
 *                 5 level paging would result in bits 56:48 being used for the offset into the 5th level table.
 *                 Conversely, using larger sized pages, such as 2MB or 1GB, also change the address. 2MB pages would result in there
 *                 being no pde, meaning bits 20:0 are the offset into the physical page. 1GB would mean that bits 29:0 are the offset
 *                 in the page.
 *
 * Page tables are explained further down in another comment, but it's important to note that they contain special addressing.
 * Page tables contain a different format of address than anything else in x86-64:
 * |63|62       52|51                                    12|11         0|
 *   0 00000000000 0000000000000000000000000000000000000000 000000000000
 * Meaning of Bits:
 * 63: (NX) - No execute. Controls the ability to execute code from all physical pages mapped by the table entry.
 * 62-52: Free for the OS to use as it wishes, ignored by the processor.
 * 51-12: Page Table Base Address. It's the pointer to the base of the next page (or beginning of physical page).
 *        The lower 11 bits of this address are assumed to be 0, since the pointer should be aligned to a boundary.
 * !-- It is important to note that the following, bits 11-0, are all flags. --!
 * 11-9: Free for the OS to use as it wishes, ignored by the processor.
 * 8: Global Page Bit. WallOS likely wont use these, see page 158 in Volume 2 of the AMD Manuals for reference.
 * 7: Page Size. This is only relevant if we dont want 4KB pages.
 *    If we want say, 2MB pages, this bit would be set in the PDE, and the address would point to a physical page.
 * 6: Dirty Bit. Only set on the lowest level of hierarchy (pte for 4KB pages, pde for 2MB, etc.).
 *    Set to 1 by the processor upon first write to the page. OS has to manually change the bit back to zero.
 * 5: Accessed. Much like the dirty bit, set to 1 by the processor whenever the table or page has been accessed for
 *    a read or write for the first time. Must be manually cleared by the OS.
 * 4: Page-Level Cache Disable. See "Memory Caches" on page 203 in AMD Manual Volume 2.
 * 3: Page-Level Writethrough. See "Memory Caches" on page 203 in AMD Manual Volume 2.
 * 2: User/Supervisor. If set to 1, the user is allowed to access values at that page.
 *    If zero, only the OS has access. If a user attempts to access supervisor memory a #PF occurs.
 * 1: Read/Write. If set to 0, the page, or all physical entries further down the hierarchy are read only.
 *    If set to 1, the page is able to be written to.
 * 0: Present. If set to 1, the page is present in physical memory. If 0, the cpu will throw a page fault.
 *    This page fault can be dealt with by either assigning a physical memory chunk or loading a page from a disk.
 * Important Note: The entire hierarchy MUST have the write bit set to 1 for the page to be writeable.
 *                 Any zero for the r/w bit through the hierarchy makes the page read only.
 *                 This also applies to the User/Supervisor bit, where the entire hierarchy must have the
 *                 User bit set for the memory to be accessible in ring 3.
 *
 * For a better understanding of all the above, see page 142 (section 5 - long mode paging) in Volume 2 of the AMD Manuals.
 *
 * Physical Addresses are derived from both virtual addresses and the hierarchy of page tables.
 * After parsing through each page table, the final table entry contains an address as seen above.
 * The final table contains the same structure as those before it, with one exception.
 * Bits 51-12 correspond to a physical address like before. x86_64 supports 52 bit addressing, meaning we're missing the lower 12 bytes (11-0).
 * When translating page tables before, the processor could easily assume that the next page table would start on a page boundary, meaning those bits are zero.
 * It can't do this when trying to find a certain spot in memory. Therefore, these lower 12 bytes come from the original virtual address.
 * This gives physical addresses this final structure:
 * |63        52|51                                    12|11         0|
 *  000000000000 0000000000000000000000000000000000000000 000000000000
 * Meaning of Bits:
 * 63-48: Reserved, must ALL be the same type of bit (either 0 or 1). The processor takes care of this for us.
 * 51-12: Come from the final page table entry, regardless of page size. 1GB would come from the pdp, 2MB from the pde, 4KB from the pte.
 * 11-0: Come from the original virtual address, where the lower 11 bits of the original address are the same as the physical address.
 *
 * This physical address layout isn't really touched on or described well in either the Intel or AMD manuals.
 * Quite frankly it doesn't have to be. The CPU takes care of the translation for you as long as you properly set up your tables.
 * Regardless, this entire summary is meant to make x86_64 paging less daunting, and hopefully make it easier to follow the code below.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <panic.h>
#include <klibc/logger.h>
#include <drivers/serial.h>
#include <memory/virtual_mem.h>
#include <memory/physical_mem.hpp>

/* To start out, we're defining:
 * The top level page (pml4)
 * The first kernel Page Directory Pointer table (kpdp)
 * The first kernel Page Director (kpde)
 * The first kernel Page Table (kpte)
 *
 * We're also creating a userspace table,
 *
 * There is 2MB of memory relating to each full pte, 1GB relating to each full pde, and 512GB relating to each full pdp
 * This means that a full pml4 table is (512)^4 * 4096, or 256TB of virtual addressing.
 *
 * Newer processors (2023), including Threadripper 7900WX, EPYC 9004, and Intel Ice Lake are the first processors supporting level 5 paging.
 * As of right now, I dont see the need to implement 5 level paging, we dont need 128 PB of virtual addressing.
 * 5 Level paging is limited to server processors, and at that, I see no way for anything running WallOS to ever need 128PB of virtual memory.
 * This would likely mean that there were several thousands or even millions of processes running at the same time.
 */

/* The address space in general will be a mixture of 2MB and 4Kb pages. */
// Top level table [256TB]
uint64_t pml4[TABLE_ENTRIES] __attribute__((aligned(4096)));

// pml4[511] - Our kernel is in the last 2GB of virtual memory
// This entire table will be reserved for the kernel only [512GB]
uint64_t kpdp[TABLE_ENTRIES] __attribute__((aligned(4096)));
// First entry for kpdp [1GB]
uint64_t kpde[TABLE_ENTRIES] __attribute__((aligned(4096)));
// First entry in kpde [2MB]
// The lower 1MB of this is for identity mapping, the rest is mapped to KERNEL_VIRTUAL_BASE
uint64_t kpte[TABLE_ENTRIES] __attribute__((aligned(4096)));

// pml4[1] - First table for general purpose memory. [512GB]
uint64_t pdp[TABLE_ENTRIES]  __attribute__((aligned(4096)));
// First three entries in normal pdp [1GB each]
uint64_t pde[TABLE_ENTRIES]  __attribute__((aligned(4096)));

// The framebuffer will get put in the upper limit of 4gb memory
uint64_t pde_3gb[TABLE_ENTRIES] __attribute__((aligned(4096)));

void set_page_frame(uint64_t* page, uint64_t addr) {
	/* This voodoo magic does two things
	 * (*page & ~PAGE_FRAME) - clears the upper 52 bits of the page entry. Leaves the bottom 12 alone.
	 * (addr & PAGE_FRAME) - Sets the proper bits in the entry to the entry.
	 * Since it uses and bitwise AND, and the addr should be canonical form, this copies only the important bits.
	 * It means that bits 52-12 are filled, and nothing else gets touched.
	 * It also means that addr doesn't even have to be the base pointer,
	 * although this isn't ever a problem, we always use the base.
	 */
	*page = (*page & ~PAGE_FRAME) | (addr & PAGE_FRAME);
}

extern "C" {
	extern const uint64_t kernel_end;
}

uintptr_t kernel_mapping_end = 0;

/* Just some notes for my future self.
 * We're mapping both lower memory (bottom 1MB) and upper memory to the kpdp
 * This means that both pml4[0] and pml4[511] point to the same pdp.
 * pml4[1] will be the start of user memory.
 * Because of how it works out, the lower 2MB is identity mapped, although the kernel is linked so everything after the boot structures
 * uses the virtual addresses starting at KERNEL_VIRTUAL_BASE.
 *
 * To start out, we're also not going to map any physical memory to userspace. This will be dealt with later on.
 * We're just going to give the userspace a pde, allowing 2MB pages, and mark it as not present.
 */
void Memory::initVirtualMemory() {
	/* Clear the tables */
	memset(pml4, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	memset(kpdp, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	memset(kpde, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	memset(kpte, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	memset(pdp, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	memset(pde, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	memset(pde_3gb, 0, sizeof(uint64_t) * TABLE_ENTRIES);

	/* The three most important things for us to do are:
	 * 1.) Set up the tables to point to each other
	 * 2.) Identity map the lower 1MB
	 * 3.) Map the kernels address space
	 */
	/* Kernel Memory Space */
	// pml4, mapping the upper 2GB and the lower 2MB
	set_page_frame(&(pml4[511]), ((uint64_t) kpdp - KERNEL_VIRTUAL_BASE));
	pml4[511] |= BIT_WRITE | BIT_PRESENT;
	pml4[0] = pml4[511];

	// kpdp, mapping the upper 2GB
	set_page_frame(&(kpdp[510]), ((uint64_t) kpde - KERNEL_VIRTUAL_BASE));
	kpdp[510] |= BIT_WRITE | BIT_PRESENT;
	kpdp[0] = kpdp[510];

	// Set kpdp[1] to the framebuffer
	set_page_frame(&(kpdp[3]), ((uint64_t) pde_3gb - KERNEL_VIRTUAL_BASE));
	kpdp[3] |= BIT_WRITE | BIT_PRESENT;

	// Map the lower 2MB, using 4kb pages
	set_page_frame(&(kpde[0]), ((uint64_t) kpte - KERNEL_VIRTUAL_BASE));
	kpde[0] |= BIT_WRITE | BIT_PRESENT;
	for (int i = 0; i < TABLE_ENTRIES; i++) {
		set_page_frame(&(kpte[i]), PAGE_4KB_SIZE * i);
		kpte[i] |= BIT_WRITE | BIT_PRESENT;
	}
	// The upper 1MB needs to be mapped to the upper kernel address space

	/* Map the kernel address space. */
	// The kernel starts at 1MB physical, and ends at kernel_end.

	uint64_t total_size = (uint64_t) (&kernel_end) - KERNEL_VIRTUAL_BASE;
	// To determine where we need to mark addresses for the page table, we need to figure out how many 2MB pages this takes up.
	uint64_t total_pages = (total_size + PAGE_2MB_SIZE) / PAGE_2MB_SIZE; // We add the page size to total_size so we can round up a page

	// If the kernel takes up more than 2MB of memory, we need to mark those pages.
	// If it only takes up 1 page, we've already dealt with it above when we mapped kpte.
	// We want to map the first 2MB page after the kernel for the physical map
	if (total_pages <= 511) {
		for (uint64_t i = 1; i <= total_pages; i++) {
			set_page_frame(&(kpde[i]), PAGE_2MB_SIZE * i);
			kernel_mapping_end = PAGE_2MB_SIZE * i;
			kpde[i] |= BIT_SIZE | BIT_WRITE | BIT_PRESENT;
		}
	} else {
		// We have to determine how many other pte's we need.
		// For right now, I can't see the kernel needing more than 1GB of memory, at least not at launch.
		assert("Kernel is too big.");
	}

	/* User memory space & framebuffer */
	/* We're going to set the lower 4gb to their respective pde */
	set_page_frame(&(pml4[1]), ((uint64_t) pdp - KERNEL_VIRTUAL_BASE));
	pml4[1] |= BIT_USR | BIT_WRITE | BIT_PRESENT;

	set_page_frame(&(pdp[0]), ((uint64_t) pde - KERNEL_VIRTUAL_BASE));
	pdp[0] |= BIT_USR | BIT_WRITE | BIT_PRESENT;

	uint64_t ptr = (uint64_t) pml4 - KERNEL_VIRTUAL_BASE;
	asm volatile("mov %%rax, %%cr3" ::"a"(ptr));
}

uintptr_t Memory::GetMappingEnd() {
	return kernel_mapping_end;
}


/**
 * @brief Removes the upper 12 bits and the lower 12 bits from the page frame.
 * This results in getting the physical address contained in the page.
 *
 * @param ptr
 * @return uintptr_t
 */
uintptr_t getFrame(uintptr_t ptr) {
	return (ptr & ~0xFFF0000000000FFF);
}

uintptr_t Memory::VirtToPhysBase(uintptr_t addr) {
	addr = addr & ~0x1FFFFF; // Clear the lower bytes of the addr to get the base page pointer
	int pml4_index = GET_PML4_INDEX(addr);
	int pdp_index = GET_PDPT_INDEX(addr);
	int pde_index = GET_PAGE_DIR_INDEX(addr);
	int pte_index = GET_PAGE_TABLE_INDEX(addr);
	// Extract the addresses from the pages.
	uint64_t* pdp_t = (uint64_t*) getFrame(pml4[pml4_index]);
	if (pdp_t[pdp_index] & (1 << POS_SIZE)) {
		// 1GB pages, the physical address is pdp_t entry
		return (uintptr_t) getFrame(pdp_t[pdp_index]);
	}

	uint64_t* pde_t = (uint64_t*) getFrame(pdp_t[pdp_index]);
	if (pde_t[pde_index] & (1 << POS_SIZE)) {
		// 2MB pages, the physical address is pde_t entry
		return (uintptr_t) getFrame(pde_t[pde_index]);
	}
	uint64_t* pdt_t = (uint64_t*) getFrame(pde_t[pde_index]);
	// If we made it this far, it's a 4kb page entry
	return (uintptr_t) getFrame(pdt_t[pte_index]);
}

uintptr_t physToVirt(uint64_t pml4_index, uint64_t pdp_index, uint64_t pde_index, uint64_t pte_index, uint64_t page_size) {
	if (page_size != PAGE_2MB_SIZE) return pte_index; // We'll deal with this eventually when we get 4kb pages set up. it returns pte to shut gcc up
	// We dont need the lower 21 bits, the page address should start at an aligned boundary.
	return CANONICAL_UPPER
		+ (pml4_index << PML4_OFFSET)
		+ (pdp_index << PDP_OFFSET)
		+ (pde_index << PDE_OFFSET);
}

/* We have this defined in display.c
 * It only gets used if we have an actual framebuffer, in place of the multiboot pointer.
 * We should probably just return it from this function, then pass it to display_init(),
 * but the global namespace is already polluted enough so ¯\_(ツ)_/¯
 */
extern "C" uintptr_t framebuffer_ptr;
/**
 * @brief Maps a framebuffer into both physical and virtual memory.
 * The framebuffer gets identity mapped into memory.
 *
 * @param base_addr The physical memory address of the framebuffer.
 * @param size The size of the framebuffer in bytes.
 */
void Memory::mapFramebuffer(uintptr_t base_addr, size_t size, bool text_mode) {
	// Reserve the physical memory region to prevent other allocations from using it.
	// There's a very good chance we're already in a "reserved" region for MMIO
	Memory::reserveMemory(base_addr, size);

	// If it's VGA text mode, we don't need to do anything else, 
	// it's already identity mapped (and the mapping will be ignored anyway).
	if (text_mode) {
		printf_serial("[VMM] Framebuffer is VGA Text Mode. No mapping required.\r\n");
		return;
	}

	// Calculate the number of 2MB pages required for this framebuffer.
	// Ensure we account for the size and any alignment offset from the base address.
	size_t mb_pages_taken = (size + PAGE_2MB_SIZE - 1) / PAGE_2MB_SIZE;

	// Align the physical base address to the start of a 2MB page.
	uintptr_t phys_addr = base_addr & ~0x1FFFFF;

	// We get allocate the virtual address sequentially, with the correct flags to enable write caching to *try* to speed up framebuffer writes.
	uintptr_t virt_addr = Memory::MapSequentialKernelPagesWithFlags(mb_pages_taken, phys_addr, PDE_FLAGS_WC_2MB);

	// Read the above comment on this variable
	framebuffer_ptr = virt_addr;

	printf_serial("[VMM] Framebuffer mapped: Phys 0x%llx -> Virt 0x%llx\r\n", phys_addr, virt_addr);
}

/**
 * @brief Map the next 2mb page at provided addr. This is only meant to be used before/during initialization of the physical allocator.
 * The page fault handler can't deal with non-present accesses before the physical allocator is set up.
 *
 * @param addr Address of the page to be mapped. Does NOT matter if it's the base address or not.
 */
void Memory::MapPreAllocMem(uintptr_t addr) {
	// This address will be the virtual address, including the offset from KERNEL_VIRTUAL_BASE
	// Before we set up any allocators, we use 2mb pages.
	addr = addr & ~0x1FFFFF; // Clear the lower bytes of the addr to get the base page pointer
	int pml4_index = GET_PML4_INDEX(addr);
	int pdp_index = GET_PDPT_INDEX(addr);
	int pde_index = GET_PAGE_DIR_INDEX(addr);

	// Extract the addresses from the pages.
	uint64_t* pdp_t = (uint64_t*) getFrame(pml4[pml4_index]);
	uint64_t* pde_t = (uint64_t*) getFrame(pdp_t[pdp_index]);

	// We need to map the entry. We're going to "identity" map it in a sense
	// We're still going to use the kernel offset, but it's going to be mapped immediately after the kernel binary.
	addr -= KERNEL_VIRTUAL_BASE;
	set_page_frame(&(pde_t[pde_index]), addr);
	pde_t[pde_index] |= BIT_SIZE | BIT_WRITE | BIT_PRESENT;

	kernel_mapping_end = addr + PAGE_2MB_SIZE;

}

uintptr_t Memory::NewKernelPage() {
	// printf_serial("[VMM] NewKernelPage called 0x%llx\r\n", WALLOS_RET_ADDR());
	return MapSequentialKernelPages(1);
}

#define TABLE_ENTRY_EMPTY(table, index) (!(table[index] & (1 << (BIT_PRESENT - 1))))

uintptr_t Memory::MapSequentialKernelPages(size_t pages) {
	// We need to find sequential entries in the kpdp that we can map to.
	// Each entry in kpdp is a 1GB region of memory. 
	// We start at kpdp[510], if that's full we go to kpdp[511]
	// If both of those are full, we start at kpdp[1]->kpdp[509] (index 0 is identity mapped to index 510)
	// If we somehow need more than 512GB of virtual mappings for the kernel we've messed up somewhere.
	// For our purposes, at least for now, we're only using 2MB pages.
	// Eventually I want to be able to have the allocators request that the virtual memory manager breaks down  these 2MB pages into 4KB chunks.

	/* First attempt. Check kpdp[510] and kpdp[511] for empty entry. */
	int i = 510;
	while (i <= TABLE_ENTRIES) {
		if (i == 512) i = 1; /* Second attempt. Check the rest of kpdp. */
		if (i == 509) break; // Break the loop after we loop through the entire kpdp
		uint64_t* pde_t = (uint64_t*) getFrame(kpdp[i]);
		// Each pdp entry has 512 pde entries.
		// Each pde entry corresponds to 1GB of virtual addresses.
		// Each entry in a pde is a 2MB page.
		// If I ever get around to 4KB pages, each pde contains 512 pte, each of which is 4kb pages

		uint64_t pde_base_index = 0;
		size_t current_streak = 0;

		// If the pde entry isn't present, we need to create a new pde or load one from disk
		if (TABLE_ENTRY_EMPTY(kpdp, i)) {
			// TODO: use kernel allocator to alloc new tables
			continue; // For now we're going to just continue.
		}
		for (int j = 0; j < TABLE_ENTRIES; j++) {
			if (TABLE_ENTRY_EMPTY(pde_t, j)) {
					// For simplicity's sake, I am not allocating across tables.
					// I wont allocate the end of pde[1] into the beginning of pde[2]
					// This would complicate this code to be much more messy, which I dont want to deal with right now.
				if (current_streak == 0) {
					pde_base_index = j;
				}

				current_streak++;

				if (current_streak == pages) {
					uintptr_t base_addr = Memory::PhysicalAlloc2MBSequential(pages);
					if (!base_addr) panic_s("Out of physical memory.");

					for (size_t k = 0; k < pages; k++) {
						set_page_frame(&(pde_t[pde_base_index + k]), base_addr + (PAGE_2MB_SIZE * k));
						pde_t[pde_base_index + k] |= BIT_SIZE | BIT_WRITE | BIT_PRESENT;
					}

					// TODO make this use invlpg instead of this
					// This forces a full tlb flush
					asm volatile("mov %%rax, %%cr3" ::"a"((uint64_t) pml4 - KERNEL_VIRTUAL_BASE));

					// The new virtual address must be assembled. It's a lil janky.
					// pml4 index is 511
					// pdp index is `i`
					// pde index is `j`
					// the rest is the base pointer to the address.
					return physToVirt(511, i, pde_base_index, 0, PAGE_2MB_SIZE);
				}
			} else {
				current_streak = 0;
				pde_base_index = 0;
			}
		}
		i++;
	}

	// If we still haven't found something we got a problem.
	// This will eventually be solved with swap space or something similar.
	panic_s("Kernel has run out of virtual memory space.");
	return 0; // Keep GCC happy. This is irrelevant.
}

/**
 * @brief Map sequential pages of virtual memory.
 * This assumes you've already provided/allocated the base address of the sequential physical pages you require.
 *
 * @param pages Amount of pages to map
 * @param phys_base_addr Base address of the physical pages.
 * If you have more than one page, it will automatically add 2MB_PAGE_SIZE to the base for each sequential page.
 * @return uintptr_t The base virtual memory address corresponding to the provided physical addresses.
 */
uintptr_t Memory::MapSequentialKernelPages(size_t pages, uintptr_t phys_base_addr) {
	// This is essentially an exact copy of the other sequential mapping
	// This one just assumes you have already asked the physical allocator for the pages rather than allocating it's own.

	/* First attempt. Check kpdp[510] and kpdp[511] for empty entry. */
	int i = 510;
	while (i <= TABLE_ENTRIES) {
		if (i == 512) i = 1; /* Second attempt. Check the rest of kpdp. */
		if (i == 509) break; // Break the loop after we loop through the entire kpdp
		uint64_t* pde_t = (uint64_t*) getFrame(kpdp[i]);
		// Each pdp entry has 512 pde entries.
		// Each pde entry corresponds to 1GB of virtual addresses.
		// Each entry in a pde is a 2MB page.
		// If I ever get around to 4KB pages, each pde contains 512 pte, each of which is 4kb pages

		uint64_t pde_base_index = 0;
		size_t current_streak = 0;

		// If the pde entry isn't present, we need to create a new pde or load one from disk
		if (TABLE_ENTRY_EMPTY(kpdp, i)) {
			// TODO: use kernel allocator to alloc new tables
			continue; // For now we're going to just continue.
		}
		for (int j = 0; j < TABLE_ENTRIES; j++) {
			if (TABLE_ENTRY_EMPTY(pde_t, j)) {
				// For simplicity's sake, I am not allocating across tables.
				// I wont allocate the end of pde[1] into the beginning of pde[2]
				// This would complicate this code to be much more messy, which I dont want to deal with right now.
				if (current_streak == 0) {
					pde_base_index = j;
				}

				current_streak++;

				if (current_streak == pages) {
					uintptr_t base_addr = phys_base_addr;
					if (!base_addr) {
						// panic_s("Out of physical memory.");
						return 0;
					}

					for (size_t k = 0; k < pages; k++) {
						set_page_frame(&(pde_t[pde_base_index + k]), base_addr + (PAGE_2MB_SIZE * k));
						pde_t[pde_base_index + k] |= BIT_SIZE | BIT_WRITE | BIT_PRESENT;
					}

					// TODO make this use invlpg instead of this
					// This forces a full tlb flush
					asm volatile("mov %%rax, %%cr3" ::"a"((uint64_t) pml4 - KERNEL_VIRTUAL_BASE));

					// The new virtual address must be assembled. It's a lil janky.
					// pml4 index is 511
					// pdp index is `i`
					// pde index is `j`
					// the rest is the base pointer to the address.
					return physToVirt(511, i, pde_base_index, 0, PAGE_2MB_SIZE);
				}
			} else {
				current_streak = 0;
				pde_base_index = 0;
			}
		}
		i++;
	}

	// If we still haven't found something we got a problem.
	// This will eventually be solved with swap space or something similar.
	panic_s("Kernel has run out of virtual memory space.");
	return 0; // Keep GCC happy. This is irrelevant.
}

/**
 * @brief Map sequential pages of virtual memory.
 * This assumes you've already provided/allocated the base address of the sequential physical pages you require.
 *
 * @param pages Amount of pages to map
 * @param phys_base_addr Base address of the physical pages.
 * If you have more than one page, it will automatically add 2MB_PAGE_SIZE to the base for each sequential page.
 * @param flags The flags to apply to the page, as defined in virtual_mem.h
 * @return uintptr_t The base virtual memory address corresponding to the provided physical addresses.
 */
uintptr_t Memory::MapSequentialKernelPagesWithFlags(size_t pages, uintptr_t phys_base_addr, uint64_t flags) {
	// This is essentially an exact copy of the other sequential mapping
	// This one just assumes you have already asked the physical allocator for the pages rather than allocating it's own.

	/* First attempt. Check kpdp[510] and kpdp[511] for empty entry. */
	int i = 510;
	while (i <= TABLE_ENTRIES) {
		if (i == 512) i = 1; /* Second attempt. Check the rest of kpdp. */
		if (i == 509) break; // Break the loop after we loop through the entire kpdp
		uint64_t* pde_t = (uint64_t*) getFrame(kpdp[i]);
		// Each pdp entry has 512 pde entries.
		// Each pde entry corresponds to 1GB of virtual addresses.
		// Each entry in a pde is a 2MB page.
		// If I ever get around to 4KB pages, each pde contains 512 pte, each of which is 4kb pages

		uint64_t pde_base_index = 0;
		size_t current_streak = 0;

		// If the pde entry isn't present, we need to create a new pde or load one from disk
		if (TABLE_ENTRY_EMPTY(kpdp, i)) {
			// TODO: use kernel allocator to alloc new tables
			continue; // For now we're going to just continue.
		}
		for (int j = 0; j < TABLE_ENTRIES; j++) {
			if (TABLE_ENTRY_EMPTY(pde_t, j)) {
				// For simplicity's sake, I am not allocating across tables.
				// I wont allocate the end of pde[1] into the beginning of pde[2]
				// This would complicate this code to be much more messy, which I dont want to deal with right now.
				if (current_streak == 0) {
					pde_base_index = j;
				}

				current_streak++;

				if (current_streak == pages) {
					uintptr_t base_addr = phys_base_addr;
					if (!base_addr) {
						// panic_s("Out of physical memory.");
						return 0;
					}

					for (size_t k = 0; k < pages; k++) {
						set_page_frame(&(pde_t[pde_base_index + k]), base_addr + (PAGE_2MB_SIZE * k));
						pde_t[pde_base_index + k] |= flags;
					}

					// TODO make this use invlpg instead of this
					// This forces a full tlb flush
					asm volatile("mov %%rax, %%cr3" ::"a"((uint64_t) pml4 - KERNEL_VIRTUAL_BASE));

					// The new virtual address must be assembled. It's a lil janky.
					// pml4 index is 511
					// pdp index is `i`
					// pde index is `j`
					// the rest is the base pointer to the address.
					return physToVirt(511, i, pde_base_index, 0, PAGE_2MB_SIZE);
				}
			} else {
				current_streak = 0;
				pde_base_index = 0;
			}
		}
		i++;
	}

	// If we still haven't found something we got a problem.
	// This will eventually be solved with swap space or something similar.
	panic_s("Kernel has run out of virtual memory space.");
	return 0; // Keep GCC happy. This is irrelevant.
}

uintptr_t mapSequentialKernelPagesWithFlags(size_t pages, uintptr_t phys_base_addr, uint64_t flags) {
	return Memory::MapSequentialKernelPagesWithFlags(pages, phys_base_addr, flags);
}

/**
 * @brief Maps the provided address into the kernel address space.
 *
 * The entire 2MB page around the address will be mapped. The length is to check how many pages it takes up.
 * If (addr + len) is over the 2MB boundary, both pages will be mapped sequentially.
 *
 * @param addr The PHYSICAL address to be mapped. This will NOT work for remapping virtual addresses.
 * @param len Length of the requested mapping in bytes.
 * @return uintptr_t Virtual address corresponding to the provided physical address.
 */
uintptr_t Memory::MapKernelLocation(uintptr_t addr, size_t len) {
	// The offset from the 2MB boundary line to the base address.
	size_t addr_offset = addr & 0x1FFFFF;
	uintptr_t final_addr = addr + len;

	// Calculate the start of the first 2MB page
	uintptr_t base_page_addr = addr & ~0x1FFFFF;

	// Calculate the start of the last 2MB page (by aligning the end address DOWN)
	uintptr_t final_page_addr = (final_addr - 1) & ~0x1FFFFF;

	// The number of pages is the distance between the first and last page, 
	// divided by page size, plus 1 for the first page itself.
	int page_count = ((final_page_addr - base_page_addr) / PAGE_2MB_SIZE) + 1;

	// printf_serial("\tBase Page Addr: 0x%llx\r\n\tFinal Page Addr: 0x%llx\r\n\tBase Offset: 0x%llx\r\n", base_page_addr, final_page_addr, addr_offset);

	// uintptr_t phys_base_addr = Memory::PhysicalMarkAllocated(addr, len);

	// Mark the whole 2MB-aligned region as allocated
	Memory::PhysicalMarkAllocated(base_page_addr, PAGE_2MB_SIZE);

	// Use the 2MB-aligned base for mapping
	uintptr_t phys_base_addr = base_page_addr;
	// We're going to assume we have access to the memory at this point.
	// The only way it returns NULL is if it's reserved or already mapped, which we're just going to assume means we have access.

	// printf_serial("\tPhysical Base: 0x%llx\r\n", phys_base_addr);

	// Now that the physical allocator knows we mapped it, we can tell the virtual manager to map it to the kernel address space.
	uintptr_t allocated_addr = Memory::MapSequentialKernelPages(page_count, phys_base_addr);
	if (allocated_addr == 0) return allocated_addr;

	// printf_serial("\tAllocated Virtual: 0x%llx\r\n", allocated_addr + addr_offset);

	return (allocated_addr + addr_offset);
}

uintptr_t mapKernelLocation(uintptr_t addr, size_t len) {
	return Memory::MapKernelLocation(addr, len);
}

uintptr_t Memory::MapKernelLocationWithFlags(uintptr_t addr, size_t len, uint64_t flags) {
	// The offset from the 2MB boundary line to the base address.
	size_t addr_offset = addr & 0x1FFFFF;
	uintptr_t final_addr = addr + len;

	// Calculate the start of the first 2MB page
	uintptr_t base_page_addr = addr & ~0x1FFFFF;

	// Calculate the start of the last 2MB page (by aligning the end address DOWN)
	uintptr_t final_page_addr = (final_addr - 1) & ~0x1FFFFF;

	// The number of pages is the distance between the first and last page, 
	// divided by page size, plus 1 for the first page itself.
	int page_count = ((final_page_addr - base_page_addr) / PAGE_2MB_SIZE) + 1;

	// printf_serial("\tBase Page Addr: 0x%llx\r\n\tFinal Page Addr: 0x%llx\r\n\tBase Offset: 0x%llx\r\n", base_page_addr, final_page_addr, addr_offset);

	// uintptr_t phys_base_addr = Memory::PhysicalMarkAllocated(addr, len);

	// Mark the whole 2MB-aligned region as allocated
	Memory::PhysicalMarkAllocated(base_page_addr, PAGE_2MB_SIZE);

	// Use the 2MB-aligned base for mapping
	uintptr_t phys_base_addr = base_page_addr;
	// We're going to assume we have access to the memory at this point.
	// The only way it returns NULL is if it's reserved or already mapped, which we're just going to assume means we have access.

	// printf_serial("\tPhysical Base: 0x%llx\r\n", phys_base_addr);

	// Now that the physical allocator knows we mapped it, we can tell the virtual manager to map it to the kernel address space.
	uintptr_t allocated_addr = Memory::MapSequentialKernelPagesWithFlags(page_count, phys_base_addr, flags);
	if (allocated_addr == 0) return allocated_addr;

	// printf_serial("\tAllocated Virtual: 0x%llx\r\n", allocated_addr + addr_offset);

	return (allocated_addr + addr_offset);
}

uintptr_t mapKernelLocationWithFlags(uintptr_t addr, size_t len, uint64_t flags) {
	return Memory::MapKernelLocationWithFlags(addr, len, flags);
}

#pragma GCC diagnostic ignored "-Wunused-parameter" 
void Memory::FreeKernelPage(uintptr_t addr) {

}

uintptr_t Memory::NewUserPage() {
	return 0;
}

#pragma GCC diagnostic ignored "-Wunused-parameter" 
void Memory::FreeUserPage(uintptr_t addr) {

}

uintptr_t virt_to_phys(uintptr_t addr) {
	int pml4_index = GET_PML4_INDEX(addr);
	int pdp_index = GET_PDPT_INDEX(addr);
	int pde_index = GET_PAGE_DIR_INDEX(addr);
	int pte_index = GET_PAGE_TABLE_INDEX(addr);

	uint64_t* pdp_t = (uint64_t*) getFrame(pml4[pml4_index]);

	// 1GB page
	if (pdp_t[pdp_index] & (1ULL << POS_SIZE)) {
		uintptr_t base = (uintptr_t) getFrame(pdp_t[pdp_index]);
		uintptr_t offset = addr & ((1ULL << 30) - 1); // lower 30 bits
		return base + offset;
	}

	uint64_t* pde_t = (uint64_t*) getFrame(pdp_t[pdp_index]);

	// 2MB page
	if (pde_t[pde_index] & (1ULL << POS_SIZE)) {
		uintptr_t base = (uintptr_t) getFrame(pde_t[pde_index]);
		uintptr_t offset = addr & ((1ULL << 21) - 1); // lower 21 bits
		return base + offset;
	}

	uint64_t* pte_t = (uint64_t*) getFrame(pde_t[pde_index]);

	// 4KB page
	uintptr_t base = (uintptr_t) getFrame(pte_t[pte_index]);
	uintptr_t offset = addr & 0xFFF; // lower 12 bits

	return base + offset;
}

/* ============================================================
 * virt_mem_cli  —  interactive VMM debug interface
 * Add this block to the bottom of virtual_mem.cpp (or a new
 * translation unit that includes virtual_mem.h).
 * ============================================================ */

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// vmm_cli - allows us to pull information about the VMM that's incredibly useful for debugging.
// Most of this is reliant on serial output, a lot of this is too big to parse on screen.
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static void print_entry_flags(uint64_t entry) {
	printf_serial("  flags: %s%s%s%s%s%s%s%s%s\r\n",
		(entry & (1ULL << 63)) ? "NX " : "",
		(entry & (1 << 8)) ? "GLOBAL " : "",
		(entry & (1 << 7)) ? "PS " : "",
		(entry & (1 << 6)) ? "DIRTY " : "",
		(entry & (1 << 5)) ? "ACCESS " : "",
		(entry & (1 << 2)) ? "USR " : "",
		(entry & (1 << 1)) ? "RW " : "",
		(entry & (1 << 0)) ? "PRESENT" : "NOT-PRESENT",
		""
	);
}

static void cmd_walk(uint64_t vaddr) {
	/* walk + dump a single virtual address */
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "\n[VMM WALK] vaddr = 0x%llx\n", vaddr);
	printf_serial("\r\n[VMM WALK] vaddr = 0x%llx\r\n", vaddr);

	int pml4_i = (vaddr >> 39) & 0x1FF;
	int pdp_i = (vaddr >> 30) & 0x1FF;
	int pde_i = (vaddr >> 21) & 0x1FF;
	int pte_i = (vaddr >> 12) & 0x1FF;
	int off = (vaddr) & 0xFFF;

	printf_color(PRINT_DEFAULT_FG, PRINT_DEFAULT_BG, "  indices: pml4[%d] pdp[%d] pde[%d] pte[%d] offset=0x%x\n", pml4_i, pdp_i, pde_i, pte_i, off);
	printf_serial("  indices: pml4[%d] pdp[%d] pde[%d] pte[%d] offset=0x%x\r\n", pml4_i, pdp_i, pde_i, pte_i, off);

	/* Print one level's result to both outputs. Entry value is green if present, red if not. */
#define WALK_PRINT_LEVEL(label, idx, entry)												\
		do {																				\
			bool _p = (entry) & 1;															\
			printf_color(_p ? PRINT_COLOR_LIGHT_GREEN : PRINT_COLOR_LIGHT_RED,				\
						PRINT_DEFAULT_BG,													\
						"  " label "[%d] = 0x%llx%s\n",										\
						(idx), (uint64_t)(entry), _p ? "" : "  !! NOT PRESENT");			\
			printf_serial("  " label "[%d] = 0x%llx\r\n", (idx), (uint64_t)(entry));		\
			print_entry_flags(entry);														\
		} while (0)

	/* PML4 */
	uint64_t pml4_e = pml4[pml4_i];
	WALK_PRINT_LEVEL("PML4", pml4_i, pml4_e);
	if (!(pml4_e & 1)) {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "  Walk terminated at PML4.\n");
		return;
	}

	/* PDP */
	uint64_t* pdp_t = (uint64_t*) getFrame(pml4_e);
	uint64_t  pdp_e = pdp_t[pdp_i];
	WALK_PRINT_LEVEL("PDP ", pdp_i, pdp_e);
	if (!(pdp_e & 1)) {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "  Walk terminated at PDP.\n");
		return;
	}

	/* 1 GB page? */
	if (pdp_e & (1 << 7)) {
		uint64_t phys = getFrame(pdp_e) + (vaddr & 0x3FFFFFFF);
		printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "  1GB PAGE  ->  phys = 0x%llx\n", phys);
		printf_serial("  1GB PAGE  ->  phys = 0x%llx\r\n", phys);
		return;
	}

	/* PDE */
	uint64_t* pde_t = (uint64_t*) getFrame(pdp_e);
	uint64_t  pde_e = pde_t[pde_i];
	WALK_PRINT_LEVEL("PDE ", pde_i, pde_e);
	if (!(pde_e & 1)) {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "  Walk terminated at PDE.\n");
		return;
	}

	/* 2 MB page? */
	if (pde_e & (1 << 7)) {
		uint64_t phys = getFrame(pde_e) + (vaddr & 0x1FFFFF);
		printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG,
			"  2MB PAGE  ->  phys = 0x%llx\n", phys);
		printf_serial("  2MB PAGE  ->  phys = 0x%llx\r\n", phys);
		return;
	}

	/* PTE */
	uint64_t* pte_t = (uint64_t*) getFrame(pde_e);
	uint64_t  pte_e = pte_t[pte_i];
	WALK_PRINT_LEVEL("PTE ", pte_i, pte_e);
	if (!(pte_e & 1)) {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "  Walk terminated at PTE.\n");
		return;
	}

	uint64_t phys = getFrame(pte_e) + off;
	printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "  4KB PAGE  ->  phys = 0x%llx\n", phys);
	printf_serial("  4KB PAGE  ->  phys = 0x%llx\r\n", phys);

#undef WALK_PRINT_LEVEL
}

/* Dump all present entries in a single table
 * Bulk entry data goes to serial only; terminal just gets a header and a summary line so it doesn't get flooded.
 */
static void dump_table(const char* name, uint64_t* table, size_t entries) {
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "\n[VMM DUMP] %s\n", name);
	printf_color(PRINT_COLOR_DARK_GREY, PRINT_DEFAULT_BG, "  (full entry listing on serial)\n");

	printf_serial("\r\n[VMM DUMP] %s (%zu entries, base @ %p)\r\n", name, entries, (void*) table);

	int present_count = 0;
	for (size_t i = 0; i < entries; i++) {
		if (!(table[i] & 1)) continue;
		present_count++;
		printf_serial("  [%03zu] 0x%llx  frame=0x%llx%s%s%s%s\r\n",
			i, table[i],
			getFrame(table[i]),
			(table[i] & (1 << 7)) ? " PS" : "",
			(table[i] & (1 << 2)) ? " USR" : "",
			(table[i] & (1 << 1)) ? " RW" : "",
			(table[i] & (1ULL << 63)) ? " NX" : ""
		);
	}

	/* green if anything mapped, yellow if nothing */
	int summary_fg = (present_count > 0) ? PRINT_COLOR_LIGHT_GREEN : PRINT_COLOR_YELLOW;
	printf_color(summary_fg, PRINT_DEFAULT_BG, "  %d / %zu entries present\n", present_count, entries);
	printf_serial("  %d / %zu entries present\r\n", present_count, entries);
}

/* translate virtual address to physical */
static void cmd_v2p(uint64_t vaddr) {
	uintptr_t phys = Memory::VirtToPhysBase(vaddr);
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "\n[VMM V2P]\n");
	printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "  virt  ");
	printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG, "0x%llx\n", vaddr);
	printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "  phys  ");
	printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "0x%llx\n", (uint64_t) phys);
	printf_serial("[VMM V2P] virt 0x%llx  ->  phys 0x%llx\r\n", vaddr, (uint64_t) phys);
}

static void cmd_info(void) {
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "\n[VMM INFO]\n");
	printf_serial("\r\n[VMM INFO]\r\n");

	/* One row: label in light-grey, value in white */
#define INFO_ROW(label, fmt, ...) \
    do { \
        printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "  %-22s", label); \
        printf_color(PRINT_COLOR_WHITE,      PRINT_DEFAULT_BG, fmt "\n", ##__VA_ARGS__); \
        printf_serial("  %-22s" fmt "\r\n", label, ##__VA_ARGS__); \
    } while(0)

	INFO_ROW("KERNEL_VIRTUAL_BASE:", "0x%llx", (uint64_t) KERNEL_VIRTUAL_BASE);
	INFO_ROW("kernel_mapping_end:", "0x%llx", (uint64_t) kernel_mapping_end);
	INFO_ROW("pml4:", "virt %p  phys 0x%llx", (void*) pml4, (uint64_t) pml4 - KERNEL_VIRTUAL_BASE);
	INFO_ROW("kpdp:", "virt %p  phys 0x%llx", (void*) kpdp, (uint64_t) kpdp - KERNEL_VIRTUAL_BASE);
	INFO_ROW("kpde:", "virt %p  phys 0x%llx", (void*) kpde, (uint64_t) kpde - KERNEL_VIRTUAL_BASE);
	INFO_ROW("kpte:", "virt %p  phys 0x%llx", (void*) kpte, (uint64_t) kpte - KERNEL_VIRTUAL_BASE);
	INFO_ROW("pdp:", "virt %p  phys 0x%llx", (void*) pdp, (uint64_t) pdp - KERNEL_VIRTUAL_BASE);
	INFO_ROW("pde:", "virt %p  phys 0x%llx", (void*) pde, (uint64_t) pde - KERNEL_VIRTUAL_BASE);

	int mapped_2mb = 0;
	for (int i = 0; i < TABLE_ENTRIES; i++)
		if (kpde[i] & 1) mapped_2mb++;

	/* Yellow if suspiciously low (≤1), green otherwise */
	int mb_fg = (mapped_2mb > 1) ? PRINT_COLOR_LIGHT_GREEN : PRINT_COLOR_YELLOW;
	printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "  %-22s", "kpde mapped:");
	printf_color(mb_fg, PRINT_DEFAULT_BG, "%d entries  (%d MB)\n", mapped_2mb, mapped_2mb * 2);
	printf_serial("  %-22s%d entries  (%d MB)\r\n", "kpde mapped:", mapped_2mb, mapped_2mb * 2);

#undef INFO_ROW
}

#include <terminal/terminal.h>

extern "C" int virt_mem_cli(int argc, char** argv) {
	ws_context_t* ctx = ws_getCurrentContext();

	if (!ws_parse_args(ctx, argc, argv) || !ws_has_arg(ctx, "command")) {
		ws_executeCommand("help vmm");
		return 0;
	}

	const char* cmd = ws_get_generic(ctx, "command");

	if (strcmp(cmd, "info") == 0) {
		cmd_info();

	} else if (strcmp(cmd, "walk") == 0) {
		if (!ws_has_arg(ctx, "argument")) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[VMM] usage: walk <vaddr>\n");
			return 1;
		}
		cmd_walk((uint64_t) strtoull(ws_get_generic(ctx, "argument"), NULL, 0));

	} else if (strcmp(cmd, "v2p") == 0) {
		if (!ws_has_arg(ctx, "argument")) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[VMM] usage: v2p <vaddr>\n");
			return 1;
		}
		cmd_v2p((uint64_t) strtoull(ws_get_generic(ctx, "argument"), NULL, 0));

	} else if (strcmp(cmd, "dump") == 0) {
		if (!ws_has_arg(ctx, "argument")) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[VMM] usage: dump <table>\n");
			return 1;
		}
		const char* tbl = ws_get_generic(ctx, "argument");
		if (strcmp(tbl, "pml4") == 0) dump_table("pml4", pml4, TABLE_ENTRIES);
		else if (strcmp(tbl, "kpdp") == 0) dump_table("kpdp", kpdp, TABLE_ENTRIES);
		else if (strcmp(tbl, "kpde") == 0) dump_table("kpde", kpde, TABLE_ENTRIES);
		else if (strcmp(tbl, "kpte") == 0) dump_table("kpte", kpte, TABLE_ENTRIES);
		else if (strcmp(tbl, "pdp") == 0) dump_table("pdp", pdp, TABLE_ENTRIES);
		else if (strcmp(tbl, "pde") == 0) dump_table("pde", pde, TABLE_ENTRIES);
		else if (strcmp(tbl, "pde3gb") == 0) dump_table("pde_3gb", pde_3gb, TABLE_ENTRIES);
		else {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG,
				"[VMM] unknown table '%s'\n       try: pml4 kpdp kpde kpte pdp pde pde3gb\n", tbl);
			printf_serial("[VMM] unknown table '%s'\r\n", tbl);
			return 1;
		}

	} else {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG,
			"[VMM] unknown command '%s' — try 'help'\n", cmd);
		printf_serial("[VMM] unknown command '%s'\r\n", cmd);
		return 1;
	}

	return 0;
}