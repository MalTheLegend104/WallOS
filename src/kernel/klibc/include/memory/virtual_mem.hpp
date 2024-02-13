#ifndef VIRTUAL_MEM_HPP
#define VIRTUAL_MEM_HPP
#include <stdint.h>
#include <stddef.h>

#define KERNEL_VIRTUAL_BASE 0xFFFFFFFF80000000ULL
// The upper 52 bytes of memory: 0b1111111111111111111111111111111111111111000000000000
#define PAGE_FRAME 0xFFFFFFFFFF000ULL
#define CANONICAL_UPPER 0xFFFF000000000000ULL
#define TABLE_ENTRIES 512 

/* Macros to make page modification not magic. */
#define GET_PML4_INDEX(page)         (((page) >> 39) & 0x1FF)
#define GET_PDPT_INDEX(page)         (((page) >> 30) & 0x1FF)
#define GET_PAGE_DIR_INDEX(page)     (((page) >> 21) & 0x1FF)
#define GET_PAGE_TABLE_INDEX(page)   (((page) >> 12) & 0x1FF)

#define BIT_NX                     0x8000000000000000ULL // Highest bit, bit 63
#define BIT_11                     0x800ULL
#define BIT_10                     0x400ULL
#define BIT_9                      0x200ULL
#define BIT_GLOBAL                 0x100ULL
#define BIT_SIZE                   0x80ULL
#define BIT_DIRTY                  0x40ULL
#define BIT_ACCESS                 0x20ULL
#define BIT_PCD                    0x10ULL
#define BIT_PWT                    0x08ULL
#define BIT_USR                    0x04ULL
#define BIT_WRITE                  0x02ULL
#define BIT_PRESENT                0x01ULL

#define POS_NX                     63
#define POS_11                     11
#define POS_10                     10
#define POS_9                      9
#define POS_GLOBAL                 8
#define POS_SIZE                   7
#define POS_DIRTY                  6
#define POS_ACCESS                 5
#define POS_PCD                    4
#define POS_PWT                    3
#define POS_USR                    2
#define POS_WRITE                  1
#define POS_PRESENT                0

#define SET_BIT_NX(page)           (page = (page | BIT_NX))
#define SET_BIT_11(page)           (page = (page | BIT_11))
#define SET_BIT_10(page)           (page = (page | BIT_10))
#define SET_BIT_9(page)            (page = (page | BIT_9))
#define SET_BIT_GLOBAL(page)       (page = (page | BIT_GLOBAL))
#define SET_BIT_SIZE(page)         (page = (page | BIT_SIZE))
#define CLEAR_BIT_DIRTY(page)      (page = (page & ~BIT_DIRTY))
#define CLEAR_BIT_ACCESS(page)     (page = (page & ~BIT_ACCESS))
#define SET_BIT_PCD(page)          (page = (page | BIT_PCD))
#define SET_BIT_PWT(page)          (page = (page | BIT_PWT))
#define SET_BIT_USR(page)          (page = (page | BIT_USR))
#define SET_BIT_WRITE(page)        (page = (page | BIT_WRITE))
#define SET_BIT_PRESENT(page)      (page = (page | BIT_PRESENT))

#define PAGE_4KB_SIZE 0x1000 
#define PAGE_2MB_SIZE 0x200000   // 512 * 4096
#define PAGE_1GB_SIZE 0x40000000 // 512 * 512 * 4096

#define PML4_OFFSET 39ULL
#define PDP_OFFSET  30ULL
#define PDE_OFFSET  21ULL
#define PTE_OFFSET  12ULL

namespace Memory {
	void initVirtualMemory();

	uintptr_t VirtToPhysBase(uintptr_t addr);
	void MapPreAllocMem(uintptr_t addr);
	void mapFramebuffer(uintptr_t base_addr, size_t size);

	void reserveMemory(uintptr_t base_addr, size_t size);

	uintptr_t NewKernelPage();
	void FreeKernelPage(uintptr_t addr);

	uintptr_t NewUserPage();
	void FreeUserPage(uintptr_t addr);

	uintptr_t GetMappingEnd();
}

#endif //VIRTUAL_MEM_HPP