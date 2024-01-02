/* Read the AMD and Intel programmers manuals to understanding how page tabels work. The wiki does a bad job in my opinion.
 * The naming of things and the general structure are heavily inspired by the AMD manuals.
 *
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
 * For a better understanding, see page 142 in Volume 2 of the AMD Manuals.
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
// The lower 1MB of this is going to be identity mapped
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
	// pml4
	set_page_frame(&(pml4[511]), ((uint64_t) kpdp - KERNEL_VIRTUAL_BASE));
	pml4[511] |= 0b11;
	pml4[0] = pml4[511];

	set_page_frame(&(pml4[1]), ((uint64_t) pdp - KERNEL_VIRTUAL_BASE));
	pml4[1] |= 0b111;


	/* The two most important things for us to do are:
	 * 1.) Identity map the lower 1MB
	 * 2.) Map the kernels address space
	 */


	// Identity map the lower 1MB (0x100000 is first byte above 1MB)
	// Map the kernel address space
}

