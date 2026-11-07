#ifndef VMM_INTERNAL_H
#define VMM_INTERNAL_H

/* Shared between the VMM's .c files. Nothing outside the VMM should include this.
 * A lot of this is poorly documented.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <memory/physical_mem.h>
#include <memory/virtual_mem.h>

#ifdef __cplusplus
extern "C" {
#endif

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Page table entries & Helpers
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

#define PTE_ADDR_4K 0x000FFFFFFFFFF000ULL
#define PTE_ADDR_2M 0x000FFFFFFFE00000ULL
#define PTE_ADDR_1G 0x000FFFFFC0000000ULL

/* Paging-level helpers for x86_64 four-level paging
 *
 * Levels are numbered by the page-table level containing the entry:
 *   1 = PT   (4KB pages)
 *   2 = PD   (2MB pages)
 *   3 = PDPT (1GB pages)
 *   4 = PML4 (512GB regions)
 */

/** Bit shift of the virtual address used to index the specified level. */
#define VMM_LVL_SHIFT(level) (12 + 9 * ((level) - 1))

/** Extracts the 9-bit page-table index for the specified level. */
#define VMM_LVL_INDEX(virt, level) (((virt) >> VMM_LVL_SHIFT(level)) & 0x1FF)

/** Size of the virtual address range covered by a single entry at the specified level. */
#define VMM_LVL_SIZE(level) (1ULL << VMM_LVL_SHIFT(level))

/**
 * @brief Software bit used to represent a mapped PROT_NONE page.
 *
 * PROT_NONE entries retain their physical frame but have PRESENT cleared.
 * The software bit distinguishes them from unmapped entries.
 * This is necessary because supervisor-mode accesses can bypass user-access checks.
 *
 * PRESENT or PROTNONE both indicate a live mapping.
 */
#define BIT_PROTNONE BIT_10

/** Returns true if an entry represents a mapped page, including PROT_NONE. */
#define VMM_ENTRY_LIVE(e) (((e) & (BIT_PRESENT | BIT_PROTNONE)) != 0)

	/**
	 * @brief Extracts the physical frame address from a page-table entry.
	 * @param entry Page-table entry.
	 * @param level Page-table level containing the entry (1 = 4KB, 2 = 2MB, 3 = 1GB).
	 * @return Physical base address of the mapped frame.
	 */
	static inline uint64_t vmm_entry_frame(uint64_t entry, int level) {
		if (level == 3) {
			return entry & PTE_ADDR_1G;
		}
		if (level == 2) {
			return entry & PTE_ADDR_2M;
		}
		return entry & PTE_ADDR_4K;
	}

	/**
	 * @brief Checks whether an address falls within the user virtual address range.
	 * @param addr Virtual address to check.
	 */
	static inline bool vmm_is_user_addr(uint64_t addr) { return addr >= USER_VIRTUAL_BASE && addr < USER_VIRTUAL_END; }

	/**
	 * @brief Rounds an address up to the next alignment boundary.
	 * @param x Value to align.
	 * @param a Alignment, which must be a power of two.
	 */
	static inline uint64_t vmm_align_up(uint64_t x, uint64_t a) { return (x + a - 1) & ~(a - 1); }

	/**
	 * @brief Rounds an address down to the previous alignment boundary.
	 * @param x Value to align.
	 * @param a Alignment, which must be a power of two.
	 */
	static inline uint64_t vmm_align_down(uint64_t x, uint64_t a) { return x & ~(a - 1); }

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// CPU Access
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// I dont want these taking up 100 lines.
	// clang-format off
	// TODO: for the 18th time, I need to break out rdmsr and wrmsr
	static inline uint64_t vmm_rdmsr(uint32_t msr) { uint32_t lo, hi; __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr)); return ((uint64_t) hi << 32) | lo; }
	static inline void vmm_wrmsr(uint32_t msr, uint64_t value) { __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t) value), "d"((uint32_t) (value >> 32)) : "memory"); }
	static inline uint64_t vmm_read_cr0(void) { uint64_t v; __asm__ volatile("mov %%cr0, %0" : "=r"(v)); return v; }
	static inline void vmm_write_cr0(uint64_t v) { __asm__ volatile("mov %0, %%cr0" : : "r"(v) : "memory"); }
	static inline uint64_t vmm_read_cr2(void) { uint64_t v; __asm__ volatile("mov %%cr2, %0" : "=r"(v)); return v; }
	static inline uint64_t vmm_read_cr3(void) { uint64_t v; __asm__ volatile("mov %%cr3, %0" : "=r"(v)); return v; }
	static inline void vmm_write_cr3(uint64_t v) { __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory"); }
	static inline uint64_t vmm_read_cr4(void) {uint64_t v;__asm__ volatile("mov %%cr4, %0" : "=r"(v));return v; }
	static inline void vmm_write_cr4(uint64_t v) {__asm__ volatile("mov %0, %%cr4" : : "r"(v) : "memory"); }
	static inline void vmm_invlpg(uint64_t virt) {__asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory"); }
	static inline uint64_t vmm_irq_save(void) {	uint64_t flags;	__asm__ volatile("pushfq; cli; popq %0" : "=r"(flags) : : "memory"); return flags; }
	static inline void vmm_irq_restore(uint64_t flags) { if (flags & (1 << 9)) { __asm__ volatile("sti" : : : "memory"); } }

	// invpcid types: 0 = one address in one PCID, 1 = one PCID, 2 = everything including globals, 3 = everything except globals
	static inline void vmm_invpcid(uint64_t type, uint64_t pcid, uint64_t addr) { struct { uint64_t pcid; uint64_t addr; } desc = {pcid, addr}; __asm__ volatile("invpcid %0, %1" : : "m"(desc), "r"(type) : "memory"); }
	// clang-format on

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Locks
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/* We can't use spinlock_t for a few reasons:
	 * 1. It depends on malloc, which needs the VMM
	 * 2. We don't want to be preempted while waiting on a VMM resource
	 *
	 * This is a small lock that works similar to a spinlock, but disables interrupts and waits with a busy-wait.
	 * It is meant to be statically defined.
	 * TODO: This should be an actual abstraction in the memory layer somewhere...
	 */

	typedef struct vmm_lock {
		volatile uint32_t v;
		uint64_t saved_flags; // Only written by the current owner
	} vmm_lock;

	// Called from the spin loop of every VMM lock so a CPU that's waiting on one still answers shootdowns
	void vmm_tlb_poll(void);

	static inline void vmm_lock_init(vmm_lock* l) {
		l->v = 0;
		l->saved_flags = 0;
	}

	static inline void vmm_lock_acquire(vmm_lock* l) {
		uint64_t flags = vmm_irq_save();

		while (__atomic_exchange_n(&l->v, 1, __ATOMIC_ACQUIRE)) {
			while (__atomic_load_n(&l->v, __ATOMIC_RELAXED)) {
				vmm_tlb_poll();
				__asm__ volatile("pause");
			}
		}

		l->saved_flags = flags;
	}

	static inline void vmm_lock_release(vmm_lock* l) {
		uint64_t flags = l->saved_flags;

		__atomic_store_n(&l->v, 0, __ATOMIC_RELEASE);
		vmm_irq_restore(flags);
	}

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Address spaces
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	struct AddressSpace {
		uint64_t* pml4;             //< Kernel virtual pointer to the PML4
		uint64_t pml4_phys;         //< Physical address of the PML4
		uint16_t pcid;              //< PCID assigned to this address space. 0 disables PCID.
		bool is_kernel;             //< True if this is the kernel address space
		volatile uint32_t refcount; //< Number of active references to this address space

		vmm_lock lock;              //< Protects the VMA list and user page tables
		vma* vma_list;              //< VMAs sorted by start address, in a doubly linked list
		vma* vma_cache;             //< Most recently found VMA, cached for repeated lookups

		uint64_t cpu_mask[VMM_MAX_CPUS / 64]; //< CPUs that may hold translations for this address space

		uint32_t id;                //< Identifier used by the VMM CLI
		address_space_t* all_next;  //< Next address space in the global list of live address spaces
	};

	// Shared kernel address space
	extern address_space_t vmm_kernel_as;

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Shared State
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/* I hate having the externs too, but it's the easiest way of doing this. */

	extern vmm_features vmm_feat;         //< Detected CPU features relevant to virtual memory management
	extern vmm_stats vmm_stat;            //< VMM statistics and counters
	extern bool vmm_use_pcid;             //< Whether PCID is enabled and used for address-space switching
	extern bool vmm_pmm_ready;            //< Whether the physical memory manager is initialized and ready for use
	extern uint64_t* vmm_kernel_pml4;     //< Kernel page-table root (PML4), accessed through its virtual address
	extern uint64_t vmm_kernel_pml4_phys; //< Physical address of the kernel PML4
	extern vmm_lock vmm_kernel_pt_lock;   //< Protects the kernel half of the page tables (direct map, arena, kernel image)

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Shared Helpers
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

// Used to increment VMM stat fields
#define VMM_STAT(field) __atomic_fetch_add(&vmm_stat.field, 1, __ATOMIC_RELAXED)

	/* Safe, unoptimized, memzero and memcopy. Meant for the fault path. */
	static inline void vmm_memzero(void* dst, size_t n) { __asm__ volatile("rep stosb" : "+D"(dst), "+c"(n) : "a"(0) : "memory"); }
	static inline void vmm_memcopy(void* dst, const void* src, size_t n) { __asm__ volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(n) : : "memory"); }

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// VMM Core
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	uint64_t* vmm_table_ptr(uint64_t phys);
	void vmm_early_pool_stats(size_t* used, size_t* total);
	uint64_t vmm_alloc_table(void); // Zeroed table. Returns its physical address, 0 if out of memory.
	void vmm_free_table(uint64_t phys);
	uint64_t vmm_leaf_flags(uint64_t generic, int level);

	/** Maps a page at the specified level, creating intermediate tables as needed. Returns VMM_OK, VMM_ENOMEM, or VMM_EEXIST if something is already there. */
	int vmm_pt_map(uint64_t* root, uint64_t virt, uint64_t phys, uint64_t hwflags, int level);

	/** Walks the page tables and returns the entry for `virt`, setting `level` to its level. */
	uint64_t* vmm_pt_walk(uint64_t* root, uint64_t virt, int* level);

	/* The kernel virtual arena. */
	uintptr_t vmm_kva_alloc(size_t size, size_t align, size_t misalign);
	void vmm_kva_free(uintptr_t start, size_t size);
	// Free ranges of the arena, for the CLI.
	void vmm_kva_dump(void (*emit)(uintptr_t start, size_t size));

	// Direct map layout, for the CLI.
	void vmm_direct_dump(void (*emit)(uint64_t start, uint64_t end));

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// VMM TLB
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/**
	 * Invalidates [start, end) for `as` on every CPU that may have it cached.
	 * Returns only after all CPUs have done it. Must be called after the page table change and before the old frame is reused.
	 */
	void vmm_tlb_flush_range(address_space_t* as, uintptr_t start, uintptr_t end);

	// Drops every cached translation tagged with the address space's PCID on all CPUs. Used before a PCID is recycled.
	void vmm_tlb_flush_context(address_space_t* as);

	void vmm_percpu_init(void);
	bool vmm_as_loaded_on_other_cpu(address_space_t* as); // Debug check for DestroyAddressSpace
	uint16_t vmm_pcid_alloc(void);
	void vmm_pcid_free(uint16_t pcid);
	address_space_t* vmm_current_as(void);
	uint64_t* vmm_current_root(void);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// VMM Address Space
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	void vmm_as_init_kernel(void);
	vma* vmm_vma_find(address_space_t* as, uintptr_t addr); // as->lock held
	void vmm_as_list(void (*emit)(address_space_t* as));
	uint64_t vmm_user_leaf_flags(uint32_t prot, bool cow);
	uint64_t* vmm_user_walk(address_space_t* as, uintptr_t addr, int* level);
	int vmm_stack_grow(address_space_t* as, vma* v, uintptr_t addr); // as->lock held
	void vmm_free_user_frame(uint64_t phys, int level); // drops one reference, frees at zero

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// VMM Fault
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	int vmm_handle_user_fault(address_space_t* as, uintptr_t addr, uint64_t error);

	/**
	 * Makes the page containing addr usable for the given access.
	 * as->lock must be held and `v` must be the VMA containing addr.
	 * Allocates / maps / breaks COW as needed.
	 * Returns VMM_OK, VMM_ENOMEM, or VMM_EFAULT.
	 */
	int vmm_fault_in_locked(address_space_t* as, vma* v, uintptr_t addr, bool write);

	extern char vmm_uaccess_copy_insn[];
	extern char vmm_uaccess_copy_fixup[];
	extern char vmm_uaccess_clear_insn[];
	extern char vmm_uaccess_clear_fixup[];
	size_t vmm_uaccess_copy_raw(void* dst, const void* src, size_t n);
	size_t vmm_uaccess_clear_raw(void* dst, size_t n);
	extern volatile uint8_t vmm_smap_on;

	void vmm_set_user_fault_callback(vmm_user_fault_cb cb);

#ifdef __cplusplus
}
#endif

#endif // VMM_INTERNAL_H
