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
#include <virtual_mem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * 5 Level paging is limited to server processors, and at that, there's plenty of bottlenecks in the way of even reaching 256TB
 */
uint64_t pml4[TABLE_ENTRIES] __attribute__((aligned(4096)));
uint64_t pdp[TABLE_ENTRIES]  __attribute__((aligned(4096)));
uint64_t pde[TABLE_ENTRIES]  __attribute__((aligned(4096)));
uint64_t pte[TABLE_ENTRIES]  __attribute__((aligned(4096)));

void initVirtualMemory() {
	/* Clear the tables */
	memset(pml4, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	memset(pdp, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	memset(pde, 0, sizeof(uint64_t) * TABLE_ENTRIES);
	memset(pde, 0, sizeof(uint64_t) * TABLE_ENTRIES);


}