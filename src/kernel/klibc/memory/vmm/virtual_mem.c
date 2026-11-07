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
 * 8: Global Page Bit. Kernel mappings set this so they survive a CR3 reload, see the PCID/global notes below.
 * 7: Page Size. This is only relevant if we dont want 4KB pages.
 *    If we want say, 2MB pages, this bit would be set in the PDE, and the address would point to a physical page.
 *    (In a 4KB page table entry this same bit is PAT instead, and for 2MB/1GB entries PAT moves up to bit 12.)
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
#include <drivers/serial.h>
#include <memory/physical_mem.h>
#include <memory/virtual_mem.h>
#include <memory/vmm_internal.h>
#include <panic.h>
#include <stdint.h>
#include <string.h>


// All of these are external linker symbols
// They're self explanitory. __kernel_* are all page aligned, start and end aren't
extern const uint64_t kernel_end;
extern const uint64_t kernel_start;
extern char __kernel_text_start[];
extern char __kernel_text_end[];
extern char __kernel_rodata_start[];
extern char __kernel_rodata_end[];

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Global State
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

vmm_features vmm_feat;
vmm_stats vmm_stat;
bool vmm_use_pcid = false;
bool vmm_pmm_ready = false;
uint64_t* vmm_kernel_pml4 = NULL;
uint64_t vmm_kernel_pml4_phys = 0;
vmm_lock vmm_kernel_pt_lock;

uintptr_t kernel_mapping_end = 0;

#define MSR_EFER 0xC0000080
#define MSR_PAT  0x277

// PAT memory type encodings (Intel Manual Vol. 3, "Memory Types")
#define PAT_UC  0x00ULL // Uncacheable
#define PAT_WC  0x01ULL // Write Combining
#define PAT_WT  0x04ULL // Write Through
#define PAT_WP  0x05ULL // Write Protected
#define PAT_WB  0x06ULL // Write Back
#define PAT_UCM 0x07ULL // UC- (uncacheable, but MTRRs may override to WC)

// Each of the 8 PAT entries is one byte, entry N lives at bits [8N+7:8N]
#define PAT_ENTRY(n, type) ((type) << ((n) * 8))

// PAT Layout: WB, WT, UC-, UC, WB, WT, UC-, WC
#define PAT_VALUE (PAT_ENTRY(0, PAT_WB) | PAT_ENTRY(1, PAT_WT) | PAT_ENTRY(2, PAT_UCM) | PAT_ENTRY(3, PAT_UC) | PAT_ENTRY(4, PAT_WB) | PAT_ENTRY(5, PAT_WT) | PAT_ENTRY(6, PAT_UCM) | PAT_ENTRY(7, PAT_WC))

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Early Boot Tables
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// We need a pool of tables for early boot before we can set up the PMM
// It's a bit of chicken-and-egg, the VMM needs the PMM, but the PMM needs virtual mappings
// We solve this with a small pool of tables for early memory.
// We solve two birds with one stone here, we also use this early pool for the direct map
static uint8_t early_pool[VMM_EARLY_POOL_PAGES * PAGE_4KB_SIZE] WALLOS_ALIGNED(4096);
static size_t early_pool_used = 0;

static inline uint64_t early_pool_phys_base(void) { return (uint64_t) early_pool - KERNEL_VIRTUAL_BASE; }

static inline bool in_early_pool(uint64_t phys) {
	uint64_t base = early_pool_phys_base();
	return phys >= base && phys < base + sizeof(early_pool);
}

void vmm_early_pool_stats(size_t* used, size_t* total) {
	*used = early_pool_used;
	*total = VMM_EARLY_POOL_PAGES;
}

uint64_t* vmm_table_ptr(uint64_t phys) {
	if (in_early_pool(phys)) {
		return (uint64_t*) (KERNEL_VIRTUAL_BASE + phys);
	}
	return (uint64_t*) (DIRECT_MAP_BASE + phys);
}

uint64_t vmm_alloc_table(void) {
	uint64_t phys;
	if (vmm_pmm_ready) {
		phys = pmm_alloc_4kb();
		if (!phys) {
			return 0;
		}
	} else {
		if (early_pool_used >= VMM_EARLY_POOL_PAGES) {
			return 0;
		}
		phys = early_pool_phys_base() + early_pool_used * PAGE_4KB_SIZE;
		early_pool_used++;
	}
	vmm_memzero(vmm_table_ptr(phys), PAGE_4KB_SIZE);
	return phys;
}

void vmm_free_table(uint64_t phys) {
	if (in_early_pool(phys)) {
		return;
	}
	pmm_free_pages(phys);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Page Table Helpers
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static inline uint64_t nx_bit(void) { return vmm_feat.nx ? BIT_NX : 0; }

/**
 * @brief Turns the generic flag format into bits for a real entry of the given level
 *
 * @param generic Generic flags from virtual_mem.h
 * @param level Level of the page table
 * @return uint64_t Flags for that level
 */
uint64_t vmm_leaf_flags(uint64_t generic, int level) {
	uint64_t hw = generic & (BIT_WRITE | BIT_USR | BIT_PWT | BIT_PCD | BIT_GLOBAL | BIT_ACCESS | BIT_DIRTY | BIT_COW);
	hw |= BIT_PRESENT;

	if (!(generic & BIT_KEXEC) || (generic & BIT_NX)) hw |= nx_bit();
	if (generic & BIT_PAT_LARGE) hw |= (level == 1) ? BIT_PAT_SMALL : BIT_PAT_LARGE;
	if (level > 1) hw |= BIT_SIZE;
	if (!vmm_feat.pge) hw &= ~BIT_GLOBAL;

	return hw;
}

uint64_t* vmm_pt_walk(uint64_t* root, uint64_t virt, int* level) {
	uint64_t* table = root;
	for (int lvl = 4; lvl >= 1; lvl--) {
		uint64_t* entry = &table[VMM_LVL_INDEX(virt, lvl)];
		uint64_t value = *entry;
		if (!(value & BIT_PRESENT) || lvl == 1 || (lvl < 4 && (value & BIT_SIZE))) {
			*level = lvl;
			return entry;
		}
		table = vmm_table_ptr(value & PTE_ADDR_4K);
	}
	*level = 1;
	return NULL; // Unreachable
}

int vmm_pt_map(uint64_t* root, uint64_t virt, uint64_t phys, uint64_t hwflags, int level) {
	// Intermediate entries are as permissive as possible
	// The leaf decides what's actually allowed
	uint64_t inter = BIT_PRESENT | BIT_WRITE | (vmm_is_user_addr(virt) ? BIT_USR : 0);

	uint64_t* table = root;
	for (int lvl = 4; lvl > level; lvl--) {
		uint64_t* entry = &table[VMM_LVL_INDEX(virt, lvl)];
		uint64_t value = *entry;
		if (!(value & BIT_PRESENT)) {
			uint64_t next = vmm_alloc_table();
			if (!next) {
				return VMM_ENOMEM;
			}
			*entry = (next & PTE_ADDR_4K) | inter;
		} else if (lvl < 4 && (value & BIT_SIZE)) {
			return VMM_EEXIST; // A bigger page is already in the way
		}
		table = vmm_table_ptr(*entry & PTE_ADDR_4K);
	}

	uint64_t* leaf = &table[VMM_LVL_INDEX(virt, level)];
	if (*leaf & BIT_PRESENT) {
		return VMM_EEXIST;
	}
	*leaf = vmm_entry_frame(phys, level) | hwflags;
	return VMM_OK;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// CPU Features
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

#include <cpuid.h>

static void detect_features(void) {
	uint32_t a, b, c, d;

	uint32_t max_leaf = __get_cpuid_max(0, NULL);

	if (__get_cpuid(1, &a, &b, &c, &d)) {
		vmm_feat.pat = d & (1 << 16);
		vmm_feat.pge = d & (1 << 13);
		vmm_feat.pcid = c & (1 << 17);
	}

	if (max_leaf >= 7 && __get_cpuid_count(7, 0, &a, &b, &c, &d)) {
		vmm_feat.smep = b & (1 << 7);
		vmm_feat.invpcid = b & (1 << 10);
		vmm_feat.smap = b & (1 << 20);
	}

	uint32_t max_ext_leaf = __get_cpuid_max(0x80000000, NULL);

	if (max_ext_leaf >= 0x80000001 && __get_cpuid(0x80000001, &a, &b, &c, &d)) {
		vmm_feat.nx = d & (1 << 20);
		vmm_feat.gb_pages = d & (1 << 26);
	}
}

volatile uint8_t vmm_smap_on = 0;

void vmm_init_cpu(void) {
	if (vmm_feat.nx) {
		vmm_wrmsr(MSR_EFER, vmm_rdmsr(MSR_EFER) | (1ULL << 11));
	}
	if (vmm_feat.pat) {
		vmm_wrmsr(MSR_PAT, PAT_VALUE);
	}

	// Kernel writes to read-only pages fault too
	// COW depends on that, so does W^X for the kernel image
	vmm_write_cr0(vmm_read_cr0() | (1ULL << 16));

	vmm_write_cr3(vmm_kernel_pml4_phys);

	uint64_t cr4 = vmm_read_cr4();
	if (vmm_feat.pge) {
		cr4 |= (1ULL << 7);
	}
	if (vmm_feat.smep) {
		cr4 |= (1ULL << 20);
	}
#if VMM_ENABLE_SMAP
	if (vmm_feat.smap) {
		cr4 |= (1ULL << 21);
		vmm_smap_on = 1;
	}
#endif
	vmm_write_cr4(cr4);
	if (vmm_use_pcid) {
		vmm_write_cr4(cr4 | (1ULL << 17));
	}

	vmm_percpu_init();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Virtual Arena
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/* We have a simple first-fit allocator over the KVA area.
 * MMIO mappings, framebuffer, and kernel stacks come from this.
 * These are all part of the static pool.
 */
typedef struct kva_node {
	uintptr_t start;
	size_t size;
	struct kva_node* next;
} kva_node;

#define KVA_NODES 512
static kva_node kva_nodes[KVA_NODES];
static kva_node* kva_head = NULL; // Free ranges, sorted by address
static kva_node* kva_unused = NULL; // Spare nodes
static vmm_lock kva_lock;

static void kva_init(void) {
	vmm_lock_init(&kva_lock);
	for (int i = 0; i < KVA_NODES; i++) {
		kva_nodes[i].next = (i + 1 < KVA_NODES) ? &kva_nodes[i + 1] : NULL;
	}
	kva_unused = &kva_nodes[1];

	kva_nodes[0].start = KVA_ARENA_BASE;
	kva_nodes[0].size = KVA_ARENA_END - KVA_ARENA_BASE;
	kva_nodes[0].next = NULL;
	kva_head = &kva_nodes[0];
}

/**
 * @brief Finds the requested amount of bytes of address space where (address % align) == misalign
 * This lets a mapping of physical memory that isn't 2MB aligned still line it's virtual and physical addresses up to 2MB pages
 *
 * @param size size of requested mapping
 * @param align alignment of mapping
 * @param misalign misalignment of the mapping
 * @return uintptr_t
 */
uintptr_t vmm_kva_alloc(size_t size, size_t align, size_t misalign) {
	size = vmm_align_up(size, PAGE_4KB_SIZE);

	vmm_lock_acquire(&kva_lock);
	kva_node* prev = NULL;
	for (kva_node* node = kva_head; node; prev = node, node = node->next) {
		uintptr_t node_end = node->start + node->size;
		uintptr_t aligned = ((node->start - misalign + align - 1) & ~(uintptr_t) (align - 1)) + misalign;
		if (aligned < node->start || aligned + size > node_end) {
			continue;
		}

		size_t before = aligned - node->start;
		size_t after = node_end - (aligned + size);

		if (before && after) {
			kva_node* extra = kva_unused;
			if (!extra) {
				continue; // Can't split this one, maybe another works
			}
			kva_unused = extra->next;
			extra->start = aligned + size;
			extra->size = after;
			extra->next = node->next;
			node->size = before;
			node->next = extra;
		} else if (before) {
			node->size = before;
		} else if (after) {
			node->start = aligned + size;
			node->size = after;
		} else {
			if (prev) {
				prev->next = node->next;
			} else {
				kva_head = node->next;
			}
			node->next = kva_unused;
			kva_unused = node;
		}

		vmm_lock_release(&kva_lock);
		return aligned;
	}
	vmm_lock_release(&kva_lock);
	return 0;
}

void vmm_kva_free(uintptr_t start, size_t size) {
	size = vmm_align_up(size, PAGE_4KB_SIZE);

	vmm_lock_acquire(&kva_lock);
	kva_node* prev = NULL;
	kva_node* node = kva_head;
	while (node && node->start < start) {
		prev = node;
		node = node->next;
	}

	bool merge_prev = prev && prev->start + prev->size == start;
	bool merge_next = node && start + size == node->start;

	if (merge_prev) {
		prev->size += size;
		if (merge_next) {
			prev->size += node->size;
			prev->next = node->next;
			node->next = kva_unused;
			kva_unused = node;
		}
	} else if (merge_next) {
		node->start = start;
		node->size += size;
	} else {
		kva_node* fresh = kva_unused;
		if (!fresh) {
			// Out of nodes
			// We lose this range of address space, which is better than corrupting the list
			vmm_lock_release(&kva_lock);
			return;
		}
		kva_unused = fresh->next;
		fresh->start = start;
		fresh->size = size;
		fresh->next = node;
		if (prev) {
			prev->next = fresh;
		} else {
			kva_head = fresh;
		}
	}
	vmm_lock_release(&kva_lock);
}

void vmm_kva_dump(void (*emit)(uintptr_t start, size_t size)) {
	vmm_lock_acquire(&kva_lock);
	for (kva_node* node = kva_head; node; node = node->next) {
		emit(node->start, node->size);
	}
	vmm_lock_release(&kva_lock);
}

/**
 * @brief Unmaps a KVA arena mapping
 *
 * @param virt Virtual address of the mapping
 * @param len Length of the mapping
 */
static void arena_unmap(uintptr_t virt, size_t len) {
	uintptr_t start = vmm_align_down(virt, PAGE_4KB_SIZE);
	uintptr_t end = vmm_align_up(virt + len, PAGE_4KB_SIZE);

	vmm_lock_acquire(&vmm_kernel_pt_lock);
	for (uintptr_t v = start; v < end;) {
		int level;
		uint64_t* entry = vmm_pt_walk(vmm_kernel_pml4, v, &level);
		if (entry && (*entry & BIT_PRESENT)) {
			uint64_t step = VMM_LVL_SIZE(level);
			*entry = 0;
			v = vmm_align_down(v, step) + step;
		} else {
			v += PAGE_4KB_SIZE;
		}
	}
	vmm_lock_release(&vmm_kernel_pt_lock);

	// Global entries survive CR3 reloads, so every CPU has to drop them explicitly. Then the address space can be reused.
	vmm_tlb_flush_range(NULL, start, end);
	vmm_kva_free(start, end - start);
}

/**
 * @brief Maps the physical address at a fresh arena address. Uses 2MB pages whenever the virtual and physical addresses both line up.
 *
 * @param phys Physical address to map
 * @param len Length of the mapping
 * @param generic Generic flags to map with
 * @return uintptr_t Virtual address of the mapping, or 0 if unable to map
 */
static uintptr_t arena_map(uint64_t phys, size_t len, uint64_t generic) {
	if (len == 0) {
		len = 1;
	}
	uint64_t offset = phys & (PAGE_4KB_SIZE - 1);
	uint64_t base = phys - offset;
	uint64_t total = vmm_align_up(offset + len, PAGE_4KB_SIZE);

	size_t align = (total >= PAGE_2MB_SIZE) ? PAGE_2MB_SIZE : PAGE_4KB_SIZE;
	uintptr_t va = vmm_kva_alloc(total, align, base & (align - 1));
	if (!va) {
		return 0;
	}

	vmm_lock_acquire(&vmm_kernel_pt_lock);
	uint64_t p = base;
	uint64_t v = va;
	uint64_t remaining = total;
	bool failed = false;
	while (remaining) {
		int level = 1;
		uint64_t step = PAGE_4KB_SIZE;
		if (!((v | p) & (PAGE_2MB_SIZE - 1)) && remaining >= PAGE_2MB_SIZE) {
			level = 2;
			step = PAGE_2MB_SIZE;
		}
		uint64_t hw = vmm_leaf_flags(generic | BIT_GLOBAL, level);
		if (vmm_pt_map(vmm_kernel_pml4, v, p, hw, level) != VMM_OK) {
			failed = true;
			break;
		}
		p += step;
		v += step;
		remaining -= step;
	}
	vmm_lock_release(&vmm_kernel_pt_lock);

	if (failed) {
		// Give back the part we mapped (flushing it), then the part we never got to.
		if (v > va) {
			arena_unmap(va, v - va);
		}
		vmm_kva_free(v, va + total - v);
		return 0;
	}
	return va + offset;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Direct Map
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

#define MAX_DIRECT_REGIONS 64
static struct {
	uint64_t start;
	uint64_t end;
} direct_regions[MAX_DIRECT_REGIONS];
static int direct_region_count = 0;

void vmm_direct_dump(void (*emit)(uint64_t start, uint64_t end)) {
	for (int i = 0; i < direct_region_count; i++) emit(direct_regions[i].start, direct_regions[i].end);
}

void vmm_map_direct_region(uintptr_t start, uintptr_t end) {
	start = vmm_align_up(start, PAGE_4KB_SIZE);
	end = vmm_align_down(end, PAGE_4KB_SIZE);

	uint64_t limit = (uint64_t) DIRECT_MAP_SLOTS * PML4_SLOT_SIZE;
	if (end > limit) {
		printf_serial("[VMM] Direct map only covers the first %llu GB of physical memory, ignoring the rest.\r\n", limit >> 30);
		end = limit;
	}
	if (end <= start) {
		return;
	}

	if (direct_region_count < MAX_DIRECT_REGIONS) {
		direct_regions[direct_region_count].start = start;
		direct_regions[direct_region_count].end = end;
		direct_region_count++;
	}

	vmm_lock_acquire(&vmm_kernel_pt_lock);
	uint64_t p = start;
	while (p < end) {
		uint64_t remaining = end - p;
		int level = 1;
		if (vmm_feat.gb_pages && !(p & (PAGE_1GB_SIZE - 1)) && remaining >= PAGE_1GB_SIZE) {
			level = 3;
		} else if (!(p & (PAGE_2MB_SIZE - 1)) && remaining >= PAGE_2MB_SIZE) {
			level = 2;
		}

		uint64_t hw = vmm_leaf_flags(BIT_WRITE | BIT_GLOBAL, level); // NX, cacheable, kernel only
		int rc = vmm_pt_map(vmm_kernel_pml4, DIRECT_MAP_BASE + p, p, hw, level);
		if (rc == VMM_ENOMEM) {
			panic_s("[VMM] Out of page table memory while building the direct map. Raise VMM_EARLY_POOL_PAGES.");
		}
		// VMM_EEXIST means a previous call already covered this, which is fine
		p += VMM_LVL_SIZE(level);
	}
	vmm_lock_release(&vmm_kernel_pt_lock);
}

void vmm_set_pmm_ready(void) {
	vmm_pmm_ready = true;
	printf_serial("[VMM] PMM is up, page tables now come from the buddy allocator. Early pool used: %zu / %d pages.\r\n", early_pool_used, VMM_EARLY_POOL_PAGES);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Init
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static void make_kernel_slot(int slot) {
	uint64_t pdpt = vmm_alloc_table();
	if (!pdpt) {
		panic_s("[VMM] Early table pool exhausted.");
	}
	vmm_kernel_pml4[slot] = pdpt | BIT_PRESENT | BIT_WRITE;
}

#include <system/idt.h>
static void vmm_user_fault(address_space_t*, uintptr_t, uint64_t err, struct interrupt_regs* regs) {
	task_fault((int) ((regs->vector & 0xFF) | ((err & 0xFFFF) << 8)));
}

/* Just some notes for my future self.
 * The kernel half of every address space is built once, here, and copied into each new PML4 (see CreateAddressSpace).
 * That only works if every kernel PML4 slot already has its PDPT, so they're all created up front.
 * pml4[0] is a ring 0 only identity map of the first (kernel size + ~2MB) of RAM. The kernel is linked so everything after the boot structures
 * uses the virtual addresses starting at KERNEL_VIRTUAL_BASE, but the boot structures, multiboot info and the AP trampoline still live low.
 * pml4[1..255] are user space, empty until an address space puts something there.
 */
// the note above isn't entirely true, but still mostly true so I left it.
// the structure is better laid out in virtual_mem.h
void vmm_init(void) {
	vmm_lock_init(&vmm_kernel_pt_lock);
	kva_init();
	detect_features();
	vmm_use_pcid = vmm_feat.pcid && vmm_feat.invpcid; // We need INVPCID to invalidate other address spaces' entries without switching to them

	printf_serial(
		"[VMM] CPU: NX=%d PAT=%d PGE=%d PCID=%d INVPCID=%d SMEP=%d SMAP=%d 1GB=%d\r\n",
		vmm_feat.nx,
		vmm_feat.pat,
		vmm_feat.pge,
		vmm_feat.pcid,
		vmm_feat.invpcid,
		vmm_feat.smep,
		vmm_feat.smap,
		vmm_feat.gb_pages
	);

	vmm_kernel_pml4_phys = vmm_alloc_table();
	if (!vmm_kernel_pml4_phys) {
		panic_s("[VMM] Early table pool exhausted.");
	}
	vmm_kernel_pml4 = vmm_table_ptr(vmm_kernel_pml4_phys);

	/* The three most important things for us to do are:
	 * 1.) Give every kernel slot its PDPT
	 * 2.) Map the kernel's address space (and keep the low identity map)
	 * 3.) Switch to it
	 */
	for (int i = 0; i < DIRECT_MAP_SLOTS; i++) {
		make_kernel_slot(GET_PML4_INDEX(DIRECT_MAP_BASE) + i);
	}
	for (int i = 0; i < KVA_ARENA_SLOTS; i++) {
		make_kernel_slot(GET_PML4_INDEX(KVA_ARENA_BASE) + i);
	}
	make_kernel_slot(511);

	// We need to map the kernel image
	// Starts at 1MB physical and ends at kernel_end
	uint64_t image_start = (uint64_t) &kernel_start;
	if (image_start >= KERNEL_VIRTUAL_BASE) {
		image_start -= KERNEL_VIRTUAL_BASE;
	}
	uint64_t image_end = (uint64_t) &kernel_end - KERNEL_VIRTUAL_BASE;
	image_start = vmm_align_down(image_start, PAGE_4KB_SIZE);
	image_end = vmm_align_up(image_end, PAGE_4KB_SIZE);

	// To determine where we need to mark addresses for the page table, we need to figure out how many 2MB pages this takes up.
	uint64_t total_pages = ((uint64_t) &kernel_end - KERNEL_VIRTUAL_BASE + PAGE_2MB_SIZE) / PAGE_2MB_SIZE; // We add the page size so we can round up a page
	uint64_t low_end = (total_pages + 1) * PAGE_2MB_SIZE;
	kernel_mapping_end = PAGE_2MB_SIZE * total_pages;

	// Section permissions
	uint64_t text_lo = 0, text_hi = 0, ro_lo = 0, ro_hi = 0;
	uintptr_t syms[4] = {(uintptr_t) __kernel_text_start, (uintptr_t) __kernel_text_end, (uintptr_t) __kernel_rodata_start, (uintptr_t) __kernel_rodata_end};
	text_lo = syms[0] - KERNEL_VIRTUAL_BASE;
	text_hi = syms[1] - KERNEL_VIRTUAL_BASE;
	ro_lo = syms[2] - KERNEL_VIRTUAL_BASE;
	ro_hi = syms[3] - KERNEL_VIRTUAL_BASE;

	for (uint64_t p = 0; p < low_end; p += PAGE_4KB_SIZE) {
		uint64_t hw = BIT_PRESENT | BIT_GLOBAL;
		bool in_image = p >= image_start && p < image_end;

		if (in_image && p >= text_lo && p < text_hi) {
			// RX
		} else if (in_image && p >= ro_lo && p < ro_hi) {
			hw |= nx_bit(); // R
		} else {
			hw |= BIT_WRITE | nx_bit(); // RW, data/bss and everything outside the image
		}
		if (!vmm_feat.pge) {
			hw &= ~BIT_GLOBAL;
		}

		if (vmm_pt_map(vmm_kernel_pml4, KERNEL_VIRTUAL_BASE + p, p, hw, 1) != VMM_OK) {
			panic_s("[VMM] Failed to map the kernel image.");
		}
	}

	// Low identity map, 2MB pages, supervisor only
	for (uint64_t p = 0; p < low_end; p += PAGE_2MB_SIZE) {
		uint64_t hw = BIT_PRESENT | BIT_WRITE | BIT_SIZE;
		if (vmm_pt_map(vmm_kernel_pml4, p, p, hw, 2) != VMM_OK) {
			panic_s("[VMM] Failed to build the low identity map.");
		}
	}

	vmm_as_init_kernel();
	vmm_init_cpu();

	printf_serial("[VMM] Kernel tables live. Early pool used: %zu / %d pages. Mapped [0x0, 0x%llx).\r\n", early_pool_used, VMM_EARLY_POOL_PAGES, low_end);

	vmm_set_user_fault_callback(vmm_user_fault);
}

uintptr_t vmm_get_mapping_end(void) { return kernel_mapping_end; }
const vmm_features* vmm_get_features(void) { return &vmm_feat; }
const vmm_stats* vmm_get_stats(void) { return &vmm_stat; }

uint64_t vmm_cache_flags(vmm_cache_t type) {
	switch (type) {
		case VMM_CACHE_WT:       return VMM_CACHE_BITS_WT;
		case VMM_CACHE_UC_MINUS: return VMM_CACHE_BITS_UC_MINUS;
		case VMM_CACHE_UC:       return VMM_CACHE_BITS_UC;
		case VMM_CACHE_WC:       return VMM_CACHE_BITS_WC;
		case VMM_CACHE_WB:
		default:                 return VMM_CACHE_BITS_WB;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Translation
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/**
 * @brief Finds the page containing the address
 */
static bool translate(uintptr_t addr, uint64_t* frame, uint64_t* page_size) {
	// User tables can be torn down by another thread of the process at any time, so walk them under the lock
	// Kernel tables are never freed
	address_space_t* as = vmm_is_user_addr(addr) ? vmm_current_as() : NULL;
	if (as) {
		vmm_lock_acquire(&as->lock);
	}

	int level;
	uint64_t* entry = vmm_pt_walk(as ? as->pml4 : vmm_kernel_pml4, addr, &level);
	bool ok = entry && (*entry & BIT_PRESENT);
	if (ok) {
		*frame = vmm_entry_frame(*entry, level);
		*page_size = VMM_LVL_SIZE(level);
	}

	if (as) {
		vmm_lock_release(&as->lock);
	}
	return ok;
}

uintptr_t vmm_virt_to_phys_base(uintptr_t addr) {
	uint64_t frame, size;
	if (!translate(addr, &frame, &size)) {
		return 0;
	}
	return frame;
}

uintptr_t virt_to_phys(uintptr_t addr) {
	uint64_t frame, size;
	if (!translate(addr, &frame, &size)) {
		return 0;
	}
	return frame + (addr & (size - 1));
}

void* phys_to_virt(uintptr_t phys) { return (void*) vmm_phys_to_virt_addr(phys); }

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Kernel Mappings
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/* We have this defined in display.c
 * It only gets used if we have an actual framebuffer, in place of the multiboot pointer.
 * We should probably just return it from this function, then pass it to display_init(),
 * but the global namespace is already polluted enough so ¯\_(ツ)_/¯
 */
extern uintptr_t framebuffer_ptr;

/**
 * @brief Maps a framebuffer into both physical and virtual memory.
 * The framebuffer gets mapped into the kernel arena, write-combining, and framebuffer_ptr points at its first byte.
 *
 * @param base_addr The physical memory address of the framebuffer.
 * @param size The size of the framebuffer in bytes.
 */
void vmm_map_framebuffer(uintptr_t base_addr, size_t size, bool text_mode) {
	// Reserve the physical memory region to prevent other allocations from using it.
	// There's a very good chance we're already in a "reserved" region for MMIO
	pmm_reserve_memory(base_addr, size);

	// If it's VGA text mode, we don't need to do anything else, it's already identity mapped (and the mapping will be ignored anyway).
	if (text_mode) {
		printf_serial("[VMM] Framebuffer is VGA Text Mode. No mapping required.\r\n");
		return;
	}

	// Write combining (PAT entry 7) to *try* to speed up framebuffer writes.
	uintptr_t virt_addr = arena_map(base_addr, size, PDE_FLAGS_WC_2MB);
	if (!virt_addr) {
		panic_s("[VMM] Couldn't map the framebuffer.");
	}

	// Read the above comment on this variable
	framebuffer_ptr = virt_addr;

	printf_serial("[VMM] Framebuffer mapped: Phys 0x%llx -> Virt 0x%llx\r\n", base_addr, virt_addr);
}

/**
 * @brief Map the pages of a physical range into the kernel arena.
 * The generic flag format is described in virtual_mem.h.
 */
static uintptr_t map_physical(uintptr_t addr, size_t len, uint64_t flags, bool mark_in_pmm) {
	if (mark_in_pmm && vmm_pmm_ready) {
		// Tell the PMM those pages are spoken for. Only exact pages now, not the whole 2MB around them.
		uint64_t base = vmm_align_down(addr, PAGE_4KB_SIZE);
		pmm_mark_allocated(base, vmm_align_up(addr + len, PAGE_4KB_SIZE) - base);
	}
	return arena_map(addr, len, flags);
}

uintptr_t mapSequentialKernelPagesWithFlags(size_t pages, uintptr_t phys_base_addr, uint64_t flags) {
	if (!phys_base_addr || pages == 0) {
		return 0;
	}
	uintptr_t virt = arena_map(phys_base_addr, pages * PAGE_2MB_SIZE, flags);
	if (!virt) {
		panic_s("Kernel has run out of virtual memory space.");
	}
	return virt;
}

uintptr_t vmm_map_sequential_pages(size_t pages) {
	uintptr_t phys = pmm_alloc_contiguous(pages * (PAGE_2MB_SIZE / PAGE_4KB_SIZE));
	if (!phys) {
		panic_s("Out of physical memory.");
	}
	return mapSequentialKernelPagesWithFlags(pages, phys, BIT_WRITE);
}

uintptr_t mapKernelLocation(uintptr_t addr, size_t len) { return map_physical(addr, len, BIT_WRITE, true); }
uintptr_t mapKernelLocationWithFlags(uintptr_t addr, size_t len, uint64_t flags) { return map_physical(addr, len, flags, true); }

void unmapKernelLocation(uintptr_t virt, size_t len) {
	if (virt < KVA_ARENA_BASE || virt >= KVA_ARENA_END) {
		return;
	}
	arena_unmap(virt, len ? len : 1);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Kernel Pages
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

uintptr_t vmm_alloc_direct(void) {
	uintptr_t phys = pmm_alloc_pages(9);
	if (!phys) {
		panic_s("Out of physical memory.");
	}
	return vmm_phys_to_virt_addr(phys);
}

uintptr_t vmm_alloc_direct_4kb(void) {
	uintptr_t phys = pmm_alloc_4kb();
	if (!phys) {
		return 0;
	}
	return vmm_phys_to_virt_addr(phys);
}

void vmm_free_direct(uintptr_t addr) {
	if (!vmm_is_direct_map_addr(addr)) {
		return; // Not ours to free (the old FreeKernelPage never freed anything)
	}
	pmm_put(vmm_direct_map_to_phys(addr));
}

void vmm_free_direct_4kb(uintptr_t addr) {
	vmm_free_direct(addr);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Kernel stacks
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

uintptr_t vmm_alloc_kernel_stack(size_t pages) {
	if (pages == 0) {
		return 0;
	}
	size_t total = (pages + 1) * PAGE_4KB_SIZE; // +1 for the guard page at the bottom
	uintptr_t va = vmm_kva_alloc(total, PAGE_4KB_SIZE, 0);
	if (!va) {
		return 0;
	}

	size_t mapped = 0;
	vmm_lock_acquire(&vmm_kernel_pt_lock);
	for (size_t i = 0; i < pages; i++) {
		uintptr_t phys = pmm_alloc_4kb();
		if (!phys) {
			break;
		}
		vmm_memzero((void*) vmm_phys_to_virt_addr(phys), PAGE_4KB_SIZE);
		// The page at `va` itself is never mapped, that's the guard
		if (vmm_pt_map(vmm_kernel_pml4, va + (i + 1) * PAGE_4KB_SIZE, phys, vmm_leaf_flags(BIT_WRITE | BIT_GLOBAL, 1), 1) != VMM_OK) {
			pmm_put(phys);
			break;
		}
		mapped++;
	}

	if (mapped != pages) {
		// Roll back
		// Nothing ever ran on these pages so no other CPU can have cached them
		for (size_t i = 0; i < mapped; i++) {
			int level;
			uint64_t* entry = vmm_pt_walk(vmm_kernel_pml4, va + (i + 1) * PAGE_4KB_SIZE, &level);
			uint64_t phys = *entry & PTE_ADDR_4K;
			*entry = 0;
			pmm_put(phys);
		}
		vmm_lock_release(&vmm_kernel_pt_lock);
		vmm_kva_free(va, total);
		return 0;
	}
	vmm_lock_release(&vmm_kernel_pt_lock);
	return va + total;
}

void vmm_free_kernel_stack(uintptr_t top, size_t pages) {
	if (!top || pages == 0) {
		return;
	}
	size_t total = (pages + 1) * PAGE_4KB_SIZE;
	uintptr_t va = top - total;

	// Clear entries in batches, flush, and only then give the frames back
	uint64_t frames[64];
	size_t done = 0;
	while (done < pages) {
		size_t batch = pages - done;
		if (batch > 64) {
			batch = 64;
		}
		uintptr_t batch_start = va + (done + 1) * PAGE_4KB_SIZE;

		size_t count = 0;
		vmm_lock_acquire(&vmm_kernel_pt_lock);
		for (size_t i = 0; i < batch; i++) {
			int level;
			uint64_t* entry = vmm_pt_walk(vmm_kernel_pml4, batch_start + i * PAGE_4KB_SIZE, &level);
			if (entry && (*entry & BIT_PRESENT)) {
				frames[count++] = *entry & PTE_ADDR_4K;
				*entry = 0;
			}
		}
		vmm_lock_release(&vmm_kernel_pt_lock);

		vmm_tlb_flush_range(NULL, batch_start, batch_start + batch * PAGE_4KB_SIZE);
		for (size_t i = 0; i < count; i++) {
			pmm_put(frames[i]);
		}
		done += batch;
	}
	vmm_kva_free(va, total);
}
