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
 * 4: Page-Level Cache Disable. See “Memory Caches” on page 203 in AMD Manual Volume 2.
 * 3: Page-Level Writethrough. See “Memory Caches” on page 203 in AMD Manual Volume 2.
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
#include <memory/virtual_mem.hpp>
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
#include <drivers/serial.h>
/**
 * @brief Maps a framebuffer into both physical and virtual memory.
 *
 * @param base_addr The physical memory address of the framebuffer.
 * @param size The size of the framebuffer in bytes.
 */
void Memory::mapFramebuffer(uintptr_t base_addr, size_t size) {
	// We need to map the memory region provided into both physical and virtual memory.
	Memory::reserveMemory(base_addr, size);
	// The framebuffer should be in kernel memory

	// amount of 2mb sections this takes up
	size_t mb_pages_taken = (size + PAGE_2MB_SIZE) / PAGE_2MB_SIZE;

	// clear the lower 21 bytes of the ptr so we can map the virutal pages
	uintptr_t addr = base_addr & ~0x1FFFFF;
	// for the amount of pages taken by the map, we're going to map each page to virtual memory.

	for (size_t i = 0; i < mb_pages_taken; i++) {
		int pml4_index = GET_PML4_INDEX(addr);
		int pdp_index = GET_PDPT_INDEX(addr);
		int pde_index = GET_PAGE_DIR_INDEX(addr);

		// Extract the addresses from the pages.
		uint64_t* pdp_t = (uint64_t*) getFrame(pml4[pml4_index]);
		uint64_t* pde_t = (uint64_t*) getFrame(pdp_t[pdp_index]);

		set_page_frame(&(pde_t[pde_index]), addr);
		pde_t[pde_index] |= BIT_SIZE | BIT_WRITE | BIT_PRESENT;

		addr += PAGE_2MB_SIZE;
	}

	printf_serial("", Memory::VirtToPhysBase(base_addr));

	return;
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
	// We need to find an entry in the kpdp that we can map to.
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
		// If I ever get around to 4KB pages, each pde contains a pte, each of which is 512 4kb pages

		// If the pde entry isn't present, we need to create a new pde or load one from disk
		if (!(kpdp[i] & (1 << (BIT_PRESENT - 1)))) {
			// TODO use kernel_allocator to alloc new tables
			continue; // For now we're going to just continue.
		}
		for (int j = 0; j < TABLE_ENTRIES; j++) {
			if (!(pde_t[j] & (1 << (BIT_PRESENT - 1)))) {
				uintptr_t addr = Memory::PhysicalAlloc2MB();
				printf("\n0x%llx\n", addr);
				/* This will be dealt with properly at a later time.
				 * To deal with this properly I need to implement filesystems and swap space.
				 */
				if (!addr) panic_s("Out of physical memory.");

				set_page_frame(&(pde_t[j]), addr);
				pde_t[j] |= BIT_SIZE | BIT_WRITE | BIT_PRESENT;

				// TODO make this use invlpg instead of this
				// This forces a full tlb flush
				asm volatile("mov %%rax, %%cr3" ::"a"((uint64_t) pml4 - KERNEL_VIRTUAL_BASE));

				// The new virtual address must be assembled. It's a lil janky.
				// pml4 index is 511
				// pdp index is `i`
				// pde index is `j`
				// the rest is the base pointer to the address.
				return physToVirt(511, i, j, 0, PAGE_2MB_SIZE);
			}
		}
		i++;
	}

	// If we still haven't found something we got a problem.
	panic_s("Kernel has run out of virtual memory space.");
	return 0; // Keep GCC happy. This is irrelevant.
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