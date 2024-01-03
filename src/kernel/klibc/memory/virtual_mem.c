/* Read the AMD and Intel programmers manuals to get an understanding as to how page tabels work. The wiki does a bad job in my opinion.
 * The naming of things and the general structure are heavily inspired by the AMD manuals.
 *
 * Below is a summary of how the three most important addresses work in terms of paging:
 *
 * Virtual Addresses are the addresses given to a program by things like malloc() or new.
 * They are also given to the program as a whole when it is loaded into memory.
 * Virtual Addresses are laid out in a very particular way on x86_64:
 * |63            48|47     39|38     30|29     21|20     12|11         0|
 *  0000000000000000 000000000 000000000 000000000 000000000 000000000000
 *
 * Meaning of Bits:
 * 63-48: Reserved, must ALL be the same type of bit (either 0 or 1)
 * 47-39: Offset in the top level page (pml4).
 * 38-30: Offset in the PDP that the pml4 pointed to.
 * 29-21: Offset in the PDE that the PDP pointed to.
 * 20-11: Offset in the PTE that the PDE pointed to.
 * 11-0:  Offset in the page. 4095 is the max, pointing to the very last byte of the page.
 *
 *
 * Page tables are explained further down in another comment, but it's impoortant to note now that they contain special addressing.
 * Page tables contain a different format of address than anything else in x86-64:
 * |63|62       52|51                                    12|11         0|
 *   0 00000000000 0000000000000000000000000000000000000000 000000000000
 * Meaning of Bits:
 * 63: (NX) - No execute. Controls the ability to execute code from all physical pages mapped by the table entry.
 * 62-52: Free for the OS to use as it wishes, ignored by the processor.
 * 51-12: Page Table Base Address. It's the pointer to the base of the next page (or beginning of physical page).
 *        The lower 11 bits of this address are assumed to be 0, since the pointer should be to an aligned boundary.
 * !-- It is important to note that the following, bits 11-0, are all flags. --!
 * 11-9: Free for the OS to use as it wishes, ignored by the processor.
 * 8: Global Page Bit. WallOS likely wont use these, see page 158 in Volume 2 of the AMD Manuals for reference.
 * 7: Page Size. This is only relavent if we dont want 4KB pages.
 *    If we want say, 2MB pages, this bit would be set in the PDE, and the address would point to a physical page.
 * 6: Dirty Bit. Only set on the lowest level of heirarchy (pte for 4KB pages, pde for 2MB, etc.).
 *    Set to 1 by the processor upon first write to the page. OS has to manually change the bit back to zero.
 * 5: Accessed. Much like the dirty bit, set to 1 by the processor whenever the table or page has been accessed for
 *    a read or write for the first time. Must be manually cleared by the OS.
 * 4: Page-Level Cache Disable. See “Memory Caches” on page 203 in AMD Manual Volume 2.
 * 3: Page-Level Writethrough. See “Memory Caches” on page 203 in AMD Manual Volume 2.
 * 2: User/Supervisor. If set to 1, the user is allowed to access values at that page.
 *    If zero, only the OS has access. If a user attempts to access supervisor memory a #PF occurs.
 * 1: Read/Write. If set to 0, the page, or all physical entries further down the hierarchy are read only.
 *    If set to 1, the page is able to be written to.
 *    Important Note: The entire hierarchy MUST have the write bit set to 1 for the page to be writeable.
 *    Any zero for the r/w bit through the hierarchy makes the page read only.
 *
 * For a better understanding, see page 142 (section 5 - long mode paging) in Volume 2 of the AMD Manuals.
 *
 * Physical Addresses are dervied from both virtual addresses and the hierarchy of page tables.
 * After parsing through each page table, the final table entry contains.
 */
#include <memory/virtual_mem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The kernel is loaded at 0xFFFFFFFF80000000, which is 2GB below the upper limit of the virtual address space.
 * This is the structure of the base address in binary
 * |63            48|47     39|38     30|29     21|20                  0|
 *  1111111111111111 111111111 111111110 000000000 000000000000000000000
 * 63-48: Ignored
 * 47-39: (0b111111111) = 511. This means pml4[511]
 * 38-30: (0b111111110) = 510. This means kpdp[510]
 * 29-21: (0b000000000) = 0. This means kpde[0]
 * 20-0:  (0b000000000000000000000) = 0. Offset in the page. The kernel uses 2MB pages.
 * As the kernel needs more virtual memory, we fill kpde first, then expand to kpdp[511] as needed.
 * If both end up full, we start filling kpdp in reverse order from 510, starting at 509, then 508, etc.
 */
#define KERNEL_VIRTUAL_BASE 0xFFFFFFFF80000000ULL

#define TABLE_ENTRIES 512 

/* To start out, we're defining:
 * The top level page (pml4)
 * The first Page Directory Pointer table (pdp)
 * The first Page Director (pde)
 * The first Page Table (pte)
 * There is 2MB of memory relating to each full pte, 1GB relating to each full pde, and 512GB relating to each full pdp
 * This means that a full pml4 table is (512)^4 * 4096, or 256TB of virtual addressing.
 *
 * Newer processors (2023), including Threadripper 7900WX, EPYC 9004, and Intel Ice Lake are the first processors supporting level 5 paging.
 * As of right now, I dont see the need to implement 5 level paging, we dont need 128 PB of virtual addressing.
 * 5 Level paging is limited to server processors, and at that, I see no way for anything running WallOS to ever need 128PB of virtual memory.
 * This would likely mean that there were several thousands or even millions of processes running at the same time.
 */

/* When setting up the basic page tables, the kernel address space is going to be 2MB pages. The kernel allocator will deal with these 2MB pages. */
/* The general purpose address space will be a mixture of 2MB and 4Kb addresses. */
// Top level table [256TB]
uint64_t pml4[TABLE_ENTRIES] __attribute__((aligned(4096)));

// pml4[511] - Our kernel is in the last 2GB of virtual memory
// This entire table will be reserved for the kernel only [512GB]
uint64_t kpdp[TABLE_ENTRIES] __attribute__((aligned(4096)));
// First entry for kpdp [1GB]
uint64_t kpde[TABLE_ENTRIES] __attribute__((aligned(4096)));

// pml4[1] - First table for general purpose memory. [512GB] 
uint64_t pdp[TABLE_ENTRIES]  __attribute__((aligned(4096)));
// First entry in normal pdp [1GB]
uint64_t pde[TABLE_ENTRIES]  __attribute__((aligned(4096)));
// First entry in normal pde [2MB]
// The rest will be the start of malloc & userspace as we know it.
uint64_t pte[TABLE_ENTRIES] __attribute__((aligned(4096)));

// The first 48 bytes of memory: 0b1111111111111111111111111111111111111111000000000000
#define PAGE_FRAME 0xFFFFFFFFFF000ULL
void set_page_frame(uint64_t* page, uint64_t addr) {
	*page = (*page & ~PAGE_FRAME) | (addr & PAGE_FRAME);
}

#define PML4_GET_INDEX(addr) (((addr) >> 39) & 0x1FF)
#define PDPT_GET_INDEX(addr) (((addr) >> 30) & 0x1FF)
#define PAGE_DIR_GET_INDEX(addr) (((addr) >> 21) & 0x1FF)
#define PAGE_TABLE_GET_INDEX(addr) (((addr) >> 12) & 0x1FF)

/**
 * @brief The structure for entries the in final page table, pte
 * The upper 52 bits correspond to a physical address, in canonical form.
 * This really means that they are 48 bit address fields.
 * This entire union makes up only one 64bit chunk of memory.
 */
typedef union {
	struct {
		uint64_t phys : 52;
		uint64_t flags : 12;
	};
	uint64_t raw_entry;
} pte_entry_t;

/* Just some notes for my future self.
 * We're mapping both lower memory (bottom 1MB) and upper memory to the kpdp
 * This means that both pml4[0] and pml4[511] point to the same pdp.
 * pml4[1] will be the start of user memory.
 */
void initVirtualMemory() {
	/* Clear the tables */
	memset(pml4, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	memset(kpdp, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	memset(kpde, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	memset(pdp, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	memset(pde, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	memset(pte, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	/* We have to set up the tables to point to each other. */
	/* Kernel Memory Space */
	// pml4, mapping the upper 2GB and the lower 2MB
	set_page_frame(&(pml4[511]), ((uint64_t) kpdp - KERNEL_VIRTUAL_BASE));
	pml4[511] |= 0b11;
	pml4[0] = pml4[511];

	// kpdp, mapping the upper 2GB
	set_page_frame(&(kpdp[510]), ((uint64_t) kpde - KERNEL_VIRTUAL_BASE));
	kpdp[510] |= 0b11;

	// Map the lower 1MB
	set_page_frame(&(kpde[0]), ((uint64_t) pte - KERNEL_VIRTUAL_BASE));
	for (int i = 0; i < TABLE_ENTRIES / 2; i++) {

	}

	/* User memory space */
	set_page_frame(&(pml4[1]), ((uint64_t) pdp - KERNEL_VIRTUAL_BASE));
	pml4[1] |= 0b111;


	/* The two most important things for us to do are:
	 * 1.) Identity map the lower 1MB
	 * 2.) Map the kernels address space
	 */


	// Identity map the lower 1MB (0x100000 is first byte above 1MB)
	// Map the kernel address space
}

