#ifndef VIRTUAL_MEM_H
#define VIRTUAL_MEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wallos_attributes.h>

#define KERNEL_VIRTUAL_BASE 0xFFFFFFFF80000000ULL
// The upper 52 bytes of memory: 0b1111111111111111111111111111111111111111000000000000
#define PAGE_FRAME      0xFFFFFFFFFF000ULL
#define CANONICAL_UPPER 0xFFFF000000000000ULL
#define TABLE_ENTRIES   512

/* Macros to make page modification not magic. */
#define GET_PML4_INDEX(page)       (((page) >> 39) & 0x1FF)
#define GET_PDPT_INDEX(page)       (((page) >> 30) & 0x1FF)
#define GET_PAGE_DIR_INDEX(page)   (((page) >> 21) & 0x1FF)
#define GET_PAGE_TABLE_INDEX(page) (((page) >> 12) & 0x1FF)

#define BIT_NX 0x8000000000000000ULL // Highest bit, bit 63


#define BIT_PAT_SELECT 0x1000ULL // see note below, the VMM uses this bit to set the PAT bit based on page size
#define BIT_PAT_LARGE  0x1000ULL

#define BIT_11      0x800ULL
#define BIT_10      0x400ULL
#define BIT_9       0x200ULL
#define BIT_GLOBAL  0x100ULL
#define BIT_SIZE    0x80ULL
#define BIT_DIRTY   0x40ULL
#define BIT_ACCESS  0x20ULL
#define BIT_PCD     0x10ULL // Page Cache Disable
#define BIT_PWT     0x08ULL // Page Write Through
#define BIT_USR     0x04ULL
#define BIT_WRITE   0x02ULL
#define BIT_PRESENT 0x01ULL

/* Bit 7 means "page size" in a PDE/PDPTE but "PAT" in a 4KB PTE.
 * The VMM will translate this for the caller, see the flags note below.
 */
#define BIT_PAT_SMALL 0x80ULL

/* Software-defined bits (the CPU ignores 9-11 and 52-62 in every entry (not including level 5 paging, which I never plan on supporting)). */
#define BIT_COW BIT_9  // Present page that is shared and write-protected on purpose. A write fault copies it.
// Bit 10 is BIT_PROTNONE, see vmm_internal.h
#define BIT_KEXEC BIT_11 // Kernel mapping API only. Leave the mapping executable. Without it the map* functions add NX.

/* PAT layout, loaded into IA32_PAT on every CPU during init.
 *   index (PAT<<2 | PCD<<1 | PWT)   type
 *   0                               WB
 *   1                               WT
 *   2                               UC-
 *   3                               UC
 *   4                               WB
 *   5                               WT
 *   6                               UC-
 *   7                               WC
 */
#define PDE_FLAGS_WC_2MB (BIT_PRESENT | BIT_WRITE | BIT_PWT | BIT_PCD | BIT_SIZE | BIT_PAT_LARGE)
#define PDE_FLAGS_UC_2MB (BIT_PRESENT | BIT_WRITE | BIT_PCD | BIT_PWT | BIT_SIZE)

/* Cache attribute bits */
#define VMM_CACHE_BITS_WB       0ULL
#define VMM_CACHE_BITS_WT       (BIT_PWT)
#define VMM_CACHE_BITS_UC_MINUS (BIT_PCD)
#define VMM_CACHE_BITS_UC       (BIT_PCD | BIT_PWT)
#define VMM_CACHE_BITS_WC       (BIT_PAT_SELECT | BIT_PCD | BIT_PWT)

/* Note for all functions that take "flags":
 * It's the generic format for most: BIT_PRESENT / BIT_WRITE / BIT_USR / BIT_PWT / BIT_PCD / BIT_GLOBAL / BIT_NX.
 * BIT_PAT_SELECT (identical to BIT_PAT_LARGE) as "select PAT" no matter what page size ends up being used.
 * BIT_KEXEC keeps the mapping executable.
 * BIT_SIZE is ignored (kept for my sanity though).
 * BIT_PRESENT is always forced on (so effectively useless).
 * Mappings are NX unless BIT_KEXEC is given.
 * The VMM moves the PAT bit to the right place for 4KB entries (bit 7) or 2MB entries (bit 12) for the caller.
 * I mostly did this because I *really* didn't want to go back and fix every single place I map something in memory.
 * I will slowly clean them up as I come across them to be better.
 */

#define POS_NX      63
#define POS_11      11
#define POS_10      10
#define POS_9       9
#define POS_GLOBAL  8
#define POS_SIZE    7
#define POS_DIRTY   6
#define POS_ACCESS  5
#define POS_PCD     4
#define POS_PWT     3
#define POS_USR     2
#define POS_WRITE   1
#define POS_PRESENT 0

#define SET_BIT_NX(page)       (page = (page | BIT_NX))
#define SET_BIT_11(page)       (page = (page | BIT_11))
#define SET_BIT_10(page)       (page = (page | BIT_10))
#define SET_BIT_9(page)        (page = (page | BIT_9))
#define SET_BIT_GLOBAL(page)   (page = (page | BIT_GLOBAL))
#define SET_BIT_SIZE(page)     (page = (page | BIT_SIZE))
#define CLEAR_BIT_DIRTY(page)  (page = (page & ~BIT_DIRTY))
#define CLEAR_BIT_ACCESS(page) (page = (page & ~BIT_ACCESS))
#define SET_BIT_PCD(page)      (page = (page | BIT_PCD))
#define SET_BIT_PWT(page)      (page = (page | BIT_PWT))
#define SET_BIT_USR(page)      (page = (page | BIT_USR))
#define SET_BIT_WRITE(page)    (page = (page | BIT_WRITE))
#define SET_BIT_PRESENT(page)  (page = (page | BIT_PRESENT))

#define PAGE_4KB_SIZE 0x1000
#define PAGE_2MB_SIZE 0x200000   // 512 * 4096
#define PAGE_1GB_SIZE 0x40000000 // 512 * 512 * 4096

#define PML4_OFFSET 39ULL
#define PDP_OFFSET  30ULL
#define PDE_OFFSET  21ULL
#define PTE_OFFSET  12ULL
/* Address space layout
 *
 * PML4 slot   Virtual range                            Use
 * 0           0x0000000000000000 - 0x0000007FFFFFFFFF  Low identity map of the kernel image + boot structures. Ring 0 only.
 *                                                      The same table is shared by every address space
 * 1-255       0x0000008000000000 - 0x00007FFFFFFFFFFF  User space. Private to each address space.
 * 256-257     0xFFFF800000000000 - 0xFFFF80FFFFFFFFFF  Direct map of physical RAM (phys + DIRECT_MAP_BASE). Up to 1TB.
 * 384-391     0xFFFFC00000000000 - 0xFFFFC03FFFFFFFFF  Kernel virtual arena: MMIO, framebuffers, kernel stacks, potential future vmalloc
 * 511         0xFFFFFF8000000000 - 0xFFFFFFFFFFFFFFFF  Kernel image in the top 2GB (KERNEL_VIRTUAL_BASE)
 *
 * Every kernel slot has its PDPT allocated at init, so new address spaces can copy the kernel half once.
 *
 * Note to future self:
 * To give userspace the whole lower half later, move the identity map out of slot 0 and lower USER_VIRTUAL_BASE.
 * The lower 512GB (PML4[0]) being identity mapped isn't really required.
 * The APs only need the lower MB mapped until they can copy the base kernel mapping anyway.
 * That could (and probably should) be done in a temporary address space.
 * (reading this back, what was I thinking?)
 */
#define PML4_SLOT_SIZE    (1ULL << PML4_OFFSET) //< Size of a single PML4 virtual address slot
#define USER_VIRTUAL_BASE PML4_SLOT_SIZE        //< Start of the user virtual address range
#define USER_VIRTUAL_END  0x0000800000000000ULL //< End of the user virtual address range (exclusive)
#define USER_MMAP_BASE    (1ULL << 40)          //< Default starting address for mmap searches
#define USER_STACK_TOP    0x00007FFFFFFFF000ULL //< Default top address of the user stack

#define DIRECT_MAP_BASE  0xFFFF800000000000ULL  //< Base address of the physical memory direct map
#define DIRECT_MAP_SLOTS 2                      //< Number of PML4 slots reserved for the direct map
#define KVA_ARENA_BASE   0xFFFFC00000000000ULL  //< Base address of the kernel virtual address arena
#define KVA_ARENA_SLOTS  8                      //< Number of PML4 slots reserved for the kernel virtual arena
#define KVA_ARENA_END    (KVA_ARENA_BASE + (uint64_t) KVA_ARENA_SLOTS * PML4_SLOT_SIZE) //< End of the kernel virtual arena (exclusive)

/* Constants for the mmap family */
#define VMM_PROT_NONE  0u //< No memory access permitted
#define VMM_PROT_READ  1u //< Memory may be read
#define VMM_PROT_WRITE 2u //< Memory may be written
#define VMM_PROT_EXEC  4u //< Memory may be executed

#define VMM_MAP_FIXED           0x01u //< Map at exactly the requested address, replacing existing mappings
#define VMM_MAP_FIXED_NOREPLACE 0x02u //< Map at exactly the requested address. Fail with VMM_EEXIST if occupied.
#define VMM_MAP_GROWSDOWN       0x04u //< Allow the mapping to grow toward lower addresses on faults
#define VMM_MAP_POPULATE        0x08u //< Allocate and map all pages immediately instead of on first access
#define VMM_MAP_HUGE            0x10u //< Use 2 MiB pages, address and length must be 2 MiB aligned
#define VMM_MAP_SHARED          0x20u //< Share pages across fork

#define VMM_OK 0         //< Success return value
// TODO: should have an errno
#define VMM_ENOMEM (-12) //< Insufficient memory
#define VMM_EACCES (-13) //< Access denied
#define VMM_EFAULT (-14) //< Invalid or inaccessible memory address
#define VMM_EEXIST (-17) //< Mapping already exists
#define VMM_EINVAL (-22) //< Invalid argument

/** Interrupt vector used for TLB shootdown IPIs.
 * Should probably be the in 0xE* range with the scheduler but whatever.
 * This currently overlaps the (remapped) PIT "FPU/Coprocessor" line, which will forever be masked, so it's fine.
 */
#define VMM_TLB_VECTOR 0xFD

/** Maximum number of logical CPUs supported by the VMM. Must be a multiple of 64. Should probably match the global CPU max, but we don't enforce anything about that one */
#define VMM_MAX_CPUS 1024

// This is in idt.h
// I don't really want to include that everywhere that the VMM is used
// This also lets us be "somewhat" platform agnostic
struct interrupt_regs;
struct AddressSpace; // Opaque outside the VMM. Create with vmm_create_address_space().
typedef struct AddressSpace address_space_t;

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct vma vma;

	/**
	 * @brief Operations provided by a VMA's backing object.
	 *
	 * Allows the VMM to support memory mappings backed by external objects (files, shared memory, etc) without needing to know how those objects provide or manage their physical memory.
	 * All callbacks are optional. A VMA with ops == NULL is anonymous memory and is handled internally by the VMM.
	 */
	typedef struct vma_ops {
		/**
		 * @brief Supplies a physical frame when a page in the VMA is first accessed.
		 *
		 * Called on a not-present page fault within the VMA.
		 * The backing object is responsible for providing the physical frame.
		 * The VMM handles installing the mapping and enforcing its permissions.
		 *
		 * @param v The VMA containing the faulting address.
		 * @param file_offset Offset of the faulting page within the backing object, calculated as v->offset + (page - v->start).
		 * @param write Whether the fault was caused by a write access.
		 *
		 * @return Physical address of a 4KB frame with one reference already acquired for the new mapping, or 0 if the page cannot be provided.
		 *
		 * Called with the address space lock held and interrupts disabled.
		 * Must not sleep, block on synchronization primitives, or access user memory.
		 *
		 * For MAP_SHARED mappings, the VMM maps the page writable if permissions allow.
		 * For private mappings, the VMM maps it read-only with copy-on-write (COW) enabled.
		 */
		uintptr_t (*fault)(vma* v, uint64_t file_offset, bool write);

		/** Notifies the backing object that a VMA reference has been created. */
		void (*open)(vma* v);

		/** Notifies the backing object that a VMA reference is being released. */
		void (*close)(vma* v);
	} vma_ops;

	struct vma {
		uintptr_t start; // Page aligned, inclusive
		uintptr_t end;   // Page aligned, exclusive
		uint32_t prot;   // VMM_PROT_*
		uint32_t flags;  // VMM_MAP_*
		const vma_ops* ops;
		void* priv;      // Opaque to the VMM, for the backing object
		uint64_t offset; // Offset into the backing object of `start`
		uint64_t grow_limit; // VMM_MAP_GROWSDOWN: maximum total size in bytes
		vma* next;
		vma* prev;
	};

	typedef enum {
		VMM_CACHE_WB = 0,
		VMM_CACHE_WT,
		VMM_CACHE_UC_MINUS,
		VMM_CACHE_UC,
		VMM_CACHE_WC,
	} vmm_cache_t;

	typedef struct vmm_features {
		bool nx;
		bool pat;
		bool pge;
		bool pcid;
		bool invpcid;
		bool smep;
		bool smap;
		bool gb_pages;
	} vmm_features;

	typedef struct vmm_stats {
		uint64_t faults_total;
		uint64_t faults_demand;
		uint64_t faults_cow;
		uint64_t faults_spurious;
		uint64_t faults_stack_grow;
		uint64_t faults_user_fatal;
		uint64_t faults_uaccess_fixup;
		uint64_t tlb_local_flushes;
		uint64_t tlb_shootdowns;
		uint64_t tlb_ipis_sent;
		uint64_t as_created;
		uint64_t as_destroyed;
	} vmm_stats;

	/**
	 * Called when a user-mode access faults and can't be fixed (no VMA, wrong permission, out of memory).
	 * It should terminate the offending process/thread. If it returns, or none is set, the VMM panics.
	 */
	typedef void (*vmm_user_fault_cb)(address_space_t* as, uintptr_t addr, uint64_t error_code, struct interrupt_regs* regs);


	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Init
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/**
	 * Builds the kernel page tables and switches to them. First thing called in the kernel (after serial).
	 * Enables NX, PAT, write-protect, global pages, PCID and SMEP/SMAP when the CPU has them, on the calling CPU.
	 */
	void vmm_init(void);

	/**
	 * Per-CPU setup (EFER.NXE, PAT, CR0.WP, CR4.PGE/PCIDE/SMEP/SMAP) followed by loading the kernel CR3.
	 * The BSP gets this from vmm_init().
	 * Every AP must call it once, early, before touching anything but its stack. Needs the CPU's index to be valid already.
	 */
	void vmm_init_cpu(void);

	/**
	 * Direct-maps [start, end) of physical RAM at DIRECT_MAP_BASE using the biggest pages that fit.
	 * The PMM calls this for each usable region before it allocates mem_map.
	 */
	void vmm_map_direct_region(uintptr_t start, uintptr_t end);

	/** Called once at the end of PMM init. Page tables come out of the buddy allocator after this. */
	void vmm_set_pmm_ready(void);

	/**
	 * Tells the VMM the LAPIC is up and the vmm_smp_* functions are safe to call.
	 * Until then it assumes one CPU with index 0.
	 */
	void vmm_enable_smp(void);

	/**
	 * Marks the calling CPU as able to receive TLB shootdown IPIs.
	 * Call it on every AP once its IDT/LAPIC are up and right before it enables interrupts for good.
	 * vmm_enable_smp() must have been called (on the BSP) before any AP runs vmm_init_cpu().
	 */
	void vmm_cpu_online(void);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Direct map stuff
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/** Address of physical memory through the direct map, as an integer. */
	static inline uintptr_t vmm_phys_to_virt_addr(uintptr_t phys) { return phys + DIRECT_MAP_BASE; }
	static inline bool vmm_is_direct_map_addr(uintptr_t virt) { return virt >= DIRECT_MAP_BASE && virt < DIRECT_MAP_BASE + (uint64_t) DIRECT_MAP_SLOTS * PML4_SLOT_SIZE; }
	static inline uintptr_t vmm_direct_map_to_phys(uintptr_t virt) { return virt - DIRECT_MAP_BASE; }

	/** Same as vmm_phys_to_virt_addr(), as a pointer. */
	void* phys_to_virt(uintptr_t phys);

	const vmm_features* vmm_get_features(void);
	const vmm_stats* vmm_get_stats(void);

	/** @brief Generic-format cache attribute bits for a cache type. OR these into the flags you pass to the map* functions. */
	uint64_t vmm_cache_flags(vmm_cache_t type);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Translation
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/** @brief Physical base of the page (4KB, 2MB or 1GB, whatever the mapping uses) that contains addr. 0 if unmapped. */
	uintptr_t vmm_virt_to_phys_base(uintptr_t addr);

	/** @brief Full translation, offset included. 0 if unmapped. User addresses are looked up in the current address space. */
	uintptr_t virt_to_phys(uintptr_t addr);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Kernel Mappings
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	void vmm_map_framebuffer(uintptr_t base_addr, size_t size, bool text_mode);

	/**
	 * @brief Allocates `pages` 2MB pages of contiguous physical memory and maps them (2MB pages) contiguously in the kernel arena.
	 * @deprecated This should be based on needed size and the VMM should calculate, not the other way around.
	 */
	WALLOS_DEPRECATED("New VMM function needed. The caller shouldn't have to calculate the pages.")
	uintptr_t vmm_map_sequential_pages(size_t pages);

	/**
	 * @brief Map sequential pages of virtual memory.
	 * This assumes you've already provided/allocated the base address of the sequential physical pages you require.
	 *
	 * @param pages Amount of 2MB pages to map
	 * @param phys_base_addr Base address of the physical pages.
	 * If you have more than one page, it will automatically add 2MB_PAGE_SIZE to the base for each sequential page.
	 * @param flags The flags to apply to the page, in the generic format described near the top of this file.
	 * @return uintptr_t The base virtual memory address corresponding to the provided physical addresses.
	 *
	 * @deprecated Use mapKernelLocationWithFlags() instead. It should be up to the VMM to determine the page count, not the caller.
	 */
	WALLOS_DEPRECATED("Use mapKernelLocationWithFlags() instead.")
	uintptr_t mapSequentialKernelPagesWithFlags(size_t pages, uintptr_t phys_base_addr, uint64_t flags);

	/**
	 * @brief Maps the provided physical range into the kernel address space.
	 *
	 * Automatically determines the combination of 2MB and 4KB pages needed for the mapping.
	 *
	 * @param addr The PHYSICAL address to be mapped. This will NOT work for remapping virtual addresses.
	 * @param len Length of the requested mapping in bytes.
	 * @return uintptr_t Virtual address corresponding to the provided physical address.
	 */
	uintptr_t mapKernelLocation(uintptr_t addr, size_t len);
	/**
	 * @brief See mapKernelLocation(). Does the same but applies the supplied flags to the mapping.
	 */
	uintptr_t mapKernelLocationWithFlags(uintptr_t addr, size_t len, uint64_t flags);

	/** @brief Undoes mapKernelLocation*(). Pass the returned pointer and the same length. Does not free the physical memory. */
	void unmapKernelLocation(uintptr_t virt, size_t len);

	/** @brief 2MB of kernel memory, via the direct map. NOT zeroed. */
	uintptr_t vmm_alloc_direct(void);
	/** @brief 4KB of kernel memory, via the direct map. NOT zeroed. */
	uintptr_t vmm_alloc_direct_4kb(void);
	/** @brief Frees memory from vmm_new_kernel_page()/vmm_new_kernel_page_4kb(). Other addresses are ignored. */
	void vmm_free_direct(uintptr_t addr);
	void vmm_free_direct_page_4kb(uintptr_t addr);

	/**
	 * @brief Allocates a kernel stack of 4KB pages (count `pages`) with an unmapped guard page directly below it.
	 * @return The initial stack pointer (top of the stack), or 0 on failure.
	 */
	uintptr_t vmm_alloc_kernel_stack(size_t pages);
	void vmm_free_kernel_stack(uintptr_t top, size_t pages);

	/** @brief End of the boot-time kernel image mapping (physical offset, 2MB granular). */
	uintptr_t vmm_get_mapping_end(void);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Address Spaces
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/** Create a new address space. */
	address_space_t* vmm_create_address_space(void);

	/** Copy-on-write clone of `parent`. Returns NULL if out of memory. */
	address_space_t* vmm_fork_address_space(address_space_t* parent);

	/** Increase the references to this address space by one. */
	// void vmm_ref_address_space(address_space_t* as);
	// I originally added this thinking I would need it in an ELF loader, but I wouldn't
	// The loader will create an address space, which sets the refcount to 1 by default, and fork would call vmm_fork_* so it wouldn't touch this either.
	// leaving in case I end up needing it later though

	/** Drops a reference. The last one tears the address space down and frees every page and table. */
	void vmm_destroy_address_space(address_space_t* as);

	/** Loads the address space on this CPU. The scheduler calls this instead of writing CR3 itself. NULL means the kernel address space. */
	void vmm_switch_address_space(address_space_t* as);

	/** Get the current address space of the currently running task. */
	address_space_t* vmm_current_address_space(void);

	/** Get the global kernel address space. */
	address_space_t* vmm_kernel_address_space(void);

	/** Get the CR3 register value for the current address space (physical address of &PML4[0]) */
	uint64_t vmm_address_space_cr3(address_space_t* as);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// MMAP Stuff
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/* These are better documented because they actually need to be used outside of the VMM.
	 * I don't want to chase down the params later when I forget how all of this is implemented.
	 */

	/**
	 * @brief Creates an anonymous memory mapping.
	 *
	 * Allocates a virtual memory region in the specified address space.
	 * Physical pages are allocated on demand when the memory is first accessed.
	 *
	 * @param as Address space in which to create the mapping.
	 * @param addr Preferred starting address, or exact address if VMM_MAP_FIXED or VMM_MAP_FIXED_NOREPLACE is specified.
	 * @param len Length of the mapping in bytes.
	 * @param prot Memory protection flags (VMM_PROT_*).
	 * @param flags Mapping flags (VMM_MAP_*).
	 * @return Starting virtual address of the mapping, or a negative VMM_E* error.
	 */
	intptr_t vmm_mmap(address_space_t* as, uintptr_t addr, size_t len, uint32_t prot, uint32_t flags);

	/**
	 * @brief Creates a memory mapping with an optional backing object.
	 *
	 * Similar to vmm_mmap(), but allows the mapping to be backed by an external object through vma_ops (files, shared memory, etc).
	 *
	 * See vmm_mmap_backed() for an alternate version of this.
	 *
	 * @param as Address space in which to create the mapping.
	 * @param addr Preferred starting address, or exact address if VMM_MAP_FIXED or VMM_MAP_FIXED_NOREPLACE is specified.
	 * @param len Length of the mapping in bytes.
	 * @param prot Memory protection flags (VMM_PROT_*).
	 * @param flags Mapping flags (VMM_MAP_*).
	 * @param ops Backing object operations, or NULL for anonymous memory.
	 * @param priv Opaque backing-object data passed to the VMA callbacks.
	 * @param offset Offset into the backing object corresponding to the start of the mapping.
	 * @return Starting virtual address of the mapping, or a negative VMM_E* error.
	 */
	intptr_t vmm_mmap_ops(address_space_t* as, uintptr_t addr, size_t len, uint32_t prot, uint32_t flags, const vma_ops* ops, void* priv, uint64_t offset);

	/**
	 * @brief Removes a memory mapping from an address space.
	 *
	 * Unmaps the specified range and releases any associated physical pages and backing-object references.
	 * Portions of existing mappings may be split.
	 *
	 * @param as Address space containing the mapping.
	 * @param addr Starting virtual address of the range to unmap.
	 * @param len Length of the range in bytes.
	 * @return VMM_OK on success, or a negative VMM_E* error.
	 */
	int vmm_munmap(address_space_t* as, uintptr_t addr, size_t len);

	/**
	 * @brief Changes the protection of an existing memory mapping.
	 *
	 * Updates the access permissions for the specified range.
	 * The range must already be mapped.
	 * The VMA may be split if only part of it is modified.
	 *
	 * @param as Address space containing the mapping.
	 * @param addr Starting virtual address of the range.
	 * @param len Length of the range in bytes.
	 * @param prot New memory protection flags (VMM_PROT_*).
	 * @return VMM_OK on success, or a negative VMM_E* error.
	 */
	int vmm_mprotect(address_space_t* as, uintptr_t addr, size_t len, uint32_t prot);

	/**
	 * @brief Retrieves a copy of the VMA containing a given address.
	 *
	 * The returned structure is a snapshot.
	 * The prev and next pointers are NULL and it does not retain any references to the original VMA or backing object.
	 *
	 * @param as Address space to search.
	 * @param addr Virtual address to look up.
	 * @param out Receives the copied VMA.
	 * @return true if a VMA contains the address, false otherwise.
	 */
	bool vmm_copy_vma(address_space_t* as, uintptr_t addr, vma* out);


	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Backing Objects
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/**
	 * @brief Callback used to provide a physical frame for a backing-object mapping.
	 * Called when a page in the mapping is first accessed and needs to be supplied by the backing object.
	 *
	 * @param priv Opaque backing-object data supplied to vmm_mmap_backed().
	 * @param file_offset Offset of the faulting page within the backing object.
	 * @param write Whether the fault was caused by a write access.
	 *
	 * @return Physical address of a 4KB frame with one reference already acquired for the mapping, or 0 if the page cannot be provided.
	 *
	 * @note Called with the address space lock held and interrupts disabled. Must not sleep, block, or access user memory.
	 */
	typedef uintptr_t (*vmm_backing_fault_fn)(void* priv, uint64_t file_offset, bool write);

	/**
	 * @brief Callback for backing-object lifetime notifications.
	 * Used to acquire or release a reference to the backing object when VMAs referencing it are created or destroyed.
	 *
	 * @param priv Opaque backing-object data supplied to vmm_mmap_backed().
	 */
	typedef void (*vmm_backing_event_fn)(void* priv);

	/**
	 * @brief Creates a memory mapping backed by an external object.
	 *
	 * Alternative to vmm_mmap_ops().
	 * Instead of requiring a vma_ops structure, the callbacks are supplied individually.
	 *
	 * @param as Address space in which to create the mapping.
	 * @param addr Preferred starting address, or exact address when using VMM_MAP_FIXED or VMM_MAP_FIXED_NOREPLACE.
	 * @param len Length of the mapping in bytes.
	 * @param prot Access permissions (VMM_PROT_*).
	 * @param flags Mapping flags (VMM_MAP_*).
	 * @param fault Callback used to provide physical frames on page faults.
	 * @param open Called when a VMA referencing the backing object is created.
	 * @param close Called when a VMA referencing the backing object is destroyed.
	 * @param priv Opaque backing-object data passed to the callbacks.
	 * @param offset Offset into the backing object corresponding to the start of the mapping.
	 * @return Starting virtual address of the mapping, or a negative VMM_E* error.
	 *
	 * @note Callbacks may be NULL where appropriate. The fault callback is required for a non-anonymous mapping.
	 */
	intptr_t vmm_mmap_backed(address_space_t* as, uintptr_t addr, size_t len, uint32_t prot, uint32_t flags, vmm_backing_fault_fn fault, vmm_backing_event_fn open, vmm_backing_event_fn close, void* priv, uint64_t offset);


	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// User memory access
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/* Safe access to user pointers from the kernel. Return the number of bytes NOT copied (0 = success). They fault pages in as needed. */

	int user_access_ok(const void* ptr, size_t len);
	size_t copy_from_user(void* dst, const void* user_src, size_t n);
	size_t copy_to_user(void* user_dst, const void* src, size_t n);
	size_t clear_user(void* user_dst, size_t n);

	/**
	 * @brief Copies a NUL terminated string, at most `max` bytes including the NUL.
	 *
	 * This is a VERY common thing used in syscalls, so I gave it's own dedicated function here.
	 * TODO: The copy_to/from_* functions should probably be in their own header.
	 *
	 * @param dst Kernel destination buffer
	 * @param user_src User provided string
	 * @param max Max length of the string
	 * @return long string length, or VMM_EFAULT
	 */
	long strncpy_from_user(char* dst, const char* user_src, size_t max);

	/**
	 * @brief Open/close a window where kernel code may touch user memory directly (STAC/CLAC when SMAP is on).
	 * Only for code that can't use copy_from_user() and friends, e.g. a loader that builds a process image in place.
	 * Don't call anything that might switch address spaces or sleep inside the window.
	 * These should be in a critical section.
	 */
	void vmm_user_access_begin(void);
	void vmm_user_access_end(void);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Memory Inspection
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
#define VMM_PAGE_PRESENT 0x01u //< A physical frame is currently mapped and present in the page tables
#define VMM_PAGE_WRITE   0x02u //< The hardware mapping currently permits writes. COW pages are read-only.
#define VMM_PAGE_USER    0x04u //< The mapping is accessible from user mode
#define VMM_PAGE_NX      0x08u //< Execution is disabled for this mapping
#define VMM_PAGE_COW     0x10u //< The frame is shared and write-protected; a write fault triggers a private copy
#define VMM_PAGE_HUGE    0x20u //< The mapping uses a huge page (2MB or larger)
#define VMM_PAGE_NONE    0x40u //< A physical frame is associated with this address, but the VMA is PROT_NONE. Not present in hardware.

	/**
	 * @brief Information about a virtual-to-physical page mapping.
	 */
	typedef struct vmm_page_info {
		uint64_t phys;  //< Physical base address of the mapped frame, without the page offset
		uint32_t flags; //< Current mapping state, expressed as VMM_PAGE_* flags
		uint32_t level; //< Page-table level of the mapping: 1 = 4KB, 2 = 2MB
		uint32_t refs;  //< Current physical frame reference count
	} vmm_page_info_t;

	/** Looks at the page table entry for addr. Returns 1 and fills *out if a page is mapped there, 0 if not. Doesn't fault anything in. */
	/**
	 * @brief Inspects the page mapping at a virtual address.
	 *
	 * Does not trigger page faults or allocate missing pages.
	 * Looks at the existing page tables and reports the mapping, if one exists.
	 * @param as Address space to inspect.
	 * @param addr Virtual address to look up.
	 * @param out Receives information about the mapping.
	 * @return 1 if a mapping exists, 0 if the address is unmapped.
	 */
	int vmm_query_page(address_space_t* as, uintptr_t addr, vmm_page_info_t* out);

	/**
	 * @brief Information about a virtual memory area.
	 */
	typedef struct vmm_vma_info {
		uintptr_t start; //< First virtual address covered by the VMA (inclusive)
		uintptr_t end;   //< First virtual address beyond the VMA (exclusive)
		uint32_t prot;   //< Access permissions (VMM_PROT_*)
		uint32_t flags;  //< Mapping properties (VMM_MAP_*)
	} vmm_vma_info_t;

	/**
	 * @brief Retrieves information about the VMA containing an address.
	 *
	 * @param as Address space to inspect.
	 * @param addr Virtual address to look up.
	 * @param out Receives the VMA information.
	 * @return 1 if a VMA contains the address, 0 otherwise.
	 */
	int vmm_query_vma(address_space_t* as, uintptr_t addr, vmm_vma_info_t* out);

	/**
	 * @brief Returns the number of VMAs currently present in an address space.
	 *
	 * @param as Address space to inspect.
	 * @return Number of VMAs in the address space.
	 */
	size_t vmm_vma_count(address_space_t* as);

	/**
	 * @brief Copies data between a kernel buffer and an address space.
	 *
	 * The target address space does not need to be loaded on the current CPU.
	 * Pages are faulted in as necessary, including demand paging, COW handling, and stack growth.
	 * Access permissions are enforced using the VMA protections.
	 *
	 * Writing to an address space requires VMM_PROT_WRITE.
	 * Reading requires the address to be accessible (not VMM_PROT_NONE).
	 *
	 * For example, to load executable code, map the destination as RW, write the image, then change its permissions to RX using vmm_mprotect().
	 *
	 * @param as Address space to access.
	 * @param addr Starting virtual address in the target address space.
	 * @param src Kernel buffer containing the data to write.
	 * @param dst Kernel buffer receiving the data to read.
	 * @param n Number of bytes to transfer.
	 * @return VMM_OK on success, or VMM_EFAULT / VMM_ENOMEM on failure.
	 *
	 * @note A failure may occur after some data has already been transferred.
	 */
	int vmm_write_address_space(address_space_t* as, uintptr_t addr, const void* src, size_t n);
	int vmm_read_address_space(address_space_t* as, uintptr_t addr, void* dst, size_t n);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Physical frame allocation
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/**
	 * @brief Allocates a physical memory frame with reference counting.
	 *
	 * Uses the physical memory manager to allocate a contiguous block of 2^order pages.
	 * The returned frame starts with a reference count of 1.
	 * Allocated memory is not zeroed.
	 *
	 * @param order Allocation order: 0 = 4KB, 9 = 2MB.
	 * @return Physical base address of the allocation, or 0 if out of memory.
	 */
	uintptr_t vmm_frame_alloc(unsigned order);

	/**
	 * @brief Releases a reference to a physical frame.
	 *
	 * Decrements the frame's reference count.
	 * The physical memory is returned to the allocator when the last reference is released.
	 *
	 * @param phys Physical base address of the frame.
	 */
	void vmm_frame_put(uintptr_t phys);

	/**
	 * @brief Returns the current reference count of a physical frame.
	 *
	 * @param phys Physical base address of the frame.
	 * @return Number of outstanding references to the frame.
	 */
	uint32_t vmm_frame_refs(uintptr_t phys);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// VMM feature detection
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

#define VMM_FEAT_NX   0x01u //< No-Execute page protection is enabled
#define VMM_FEAT_PAT  0x02u //< Page Attribute Table is enabled
#define VMM_FEAT_PGE  0x04u //< Global page support is enabled
#define VMM_FEAT_PCID 0x08u //< Process-Context Identifiers are in use (requires INVPCID)
#define VMM_FEAT_SMEP 0x10u //< Supervisor Mode Execution Prevention is enabled
#define VMM_FEAT_SMAP 0x20u //< Supervisor Mode Access Prevention is enabled
#define VMM_FEAT_GB   0x40u //< 1GB page support is enabled

	/** Returns the VMM feature flags (VMM_FEAT_*) */
	uint32_t vmm_feature_flags(void);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// VMM Stats
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/**
	 * @brief Snapshot of VMM runtime statistics.
	 */
	typedef struct vmm_stats_snapshot {
		uint64_t faults_total;         //< Total page faults handled
		uint64_t faults_demand;        //< Faults resolved by demand paging
		uint64_t faults_cow;           //< Faults resolved by copy-on-write
		uint64_t faults_spurious;      //< Faults where the mapping was already valid
		uint64_t faults_stack_grow;    //< Faults that caused a stack mapping to grow
		uint64_t faults_user_fatal;    //< User faults that could not be resolved
		uint64_t faults_uaccess_fixup; //< Faults recovered through a kernel uaccess fixup
		uint64_t tlb_local_flushes;    //< TLB invalidations performed on the current CPU
		uint64_t tlb_shootdowns;       //< TLB shootdown operations initiated
		uint64_t tlb_ipis_sent;        //< IPIs sent to other CPUs for TLB invalidation
	} vmm_stats_snapshot_t;
	/** Retrieves a snapshot of the current VMM statistics */
	void vmm_stats_get(vmm_stats_snapshot_t* out);

#ifdef __cplusplus
}

#endif // __cplusplus

#endif // VIRTUAL_MEM_H
