#include <drivers/serial.h>
#include <memory/kernel_alloc.h>
#include <memory/virtual_mem.h>
#include <memory/vmm_internal.h>
#include <panic.h>
#include <string.h>

/* Address spaces are per-process
 * Each address space is a PML4 + a list of VMAs
 * The VMA list says what SHOULD be mapped. Pages get filled in lazily by the PF handler.
 * as->lock should always be grabbed before any other locks are touched
 *
 * We support Copy on Write. Page sharing rules:
 * - A frame that is mapped by more than one AS has refcount > 1 in the PMM
 * - In a private mapping, every present PTE whose frame may be shared, has BIT_COW and has the write bit CLEAR
 *   A write fault on it either takes over the frame (refcount == 1) or copies it
 * - Shared mappings (VMM_MAP_SHARED) never get BIT_COW, their PTEs stay writable
 */

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Globals and Bookkeeping
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

address_space_t vmm_kernel_as;
static address_space_t* as_list = NULL;
static vmm_lock as_list_lock;
static uint32_t next_as_id = 1;

void vmm_as_init_kernel(void) {
	memset(&vmm_kernel_as, 0, sizeof(vmm_kernel_as));
	vmm_kernel_as.pml4 = vmm_kernel_pml4;
	vmm_kernel_as.pml4_phys = vmm_kernel_pml4_phys;
	vmm_kernel_as.pcid = 0;
	vmm_kernel_as.is_kernel = true;
	vmm_kernel_as.refcount = 1;
	vmm_kernel_as.id = 0;
	vmm_lock_init(&vmm_kernel_as.lock);
	vmm_lock_init(&as_list_lock);
	as_list = &vmm_kernel_as;
}

address_space_t* vmm_kernel_address_space(void) { return &vmm_kernel_as; }
uint64_t vmm_address_space_cr3(address_space_t* as) { return as->pml4_phys; }

void vmm_as_list(void (*emit)(address_space_t* as)) {
	vmm_lock_acquire(&as_list_lock);
	for (address_space_t* a = as_list; a; a = a->all_next) {
		emit(a);
	}
	vmm_lock_release(&as_list_lock);
}

uint64_t vmm_user_leaf_flags(uint32_t prot, bool cow) {
	uint64_t nx = vmm_feat.nx ? BIT_NX : 0;
	if (!(prot & (VMM_PROT_READ | VMM_PROT_WRITE | VMM_PROT_EXEC))) {
		return BIT_PROTNONE; // PROT_NONE: not present, so user AND kernel accesses fault and the VMA check rejects them. The frame stays in the entry.
	}
	uint64_t f = BIT_PRESENT | BIT_USR;
	if ((prot & VMM_PROT_WRITE) && !cow) {
		f |= BIT_WRITE;
	}
	if (!(prot & VMM_PROT_EXEC)) {
		f |= nx;
	}
	if (cow) {
		f |= BIT_COW;
	}
	return f;
}

uint64_t* vmm_user_walk(address_space_t* as, uintptr_t addr, int* level) { return vmm_pt_walk(as->pml4, addr, level); }

void vmm_free_user_frame(uint64_t phys, int level) {
	(void) level; // A 2MB page is one order-9 block, its refcount lives on the head page like any other block
	pmm_put(phys);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// VMA List
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

vma* vmm_vma_find(address_space_t* as, uintptr_t addr) {
	vma* c = as->vma_cache;
	if (c && addr >= c->start && addr < c->end) {
		return c;
	}
	for (vma* v = as->vma_list; v; v = v->next) {
		if (addr < v->start) {
			break;
		}
		if (addr < v->end) {
			as->vma_cache = v;
			return v;
		}
	}
	return NULL;
}

// Copies the VMA under the lock, prevents us from having a use-after-free if there's a race
bool vmm_copy_vma(address_space_t* as, uintptr_t addr, vma* out) {
	vmm_lock_acquire(&as->lock);
	const vma* v = vmm_vma_find(as, addr);
	bool found = v != NULL;
	if (found) {
		*out = *v;
		out->prev = NULL;
		out->next = NULL;
	}
	vmm_lock_release(&as->lock);
	return found;
}

static vma* vma_new(uintptr_t start, uintptr_t end, uint32_t prot, uint32_t flags, const vma_ops* ops, void* priv, uint64_t offset) {
	vma* v = (vma*) kcalloc(1, sizeof(vma));
	if (!v) {
		return NULL;
	}
	v->start = start;
	v->end = end;
	v->prot = prot;
	v->flags = flags;
	v->ops = ops;
	v->priv = priv;
	v->offset = offset;
	v->grow_limit = VMM_DEFAULT_STACK_LIMIT;
	return v;
}

static void vma_link(address_space_t* as, vma* v) {
	vma* prev = NULL;
	vma* next = as->vma_list;
	while (next && next->start < v->start) {
		prev = next;
		next = next->next;
	}
	v->prev = prev;
	v->next = next;
	if (prev) {
		prev->next = v;
	} else {
		as->vma_list = v;
	}
	if (next) {
		next->prev = v;
	}
}

static void vma_unlink(address_space_t* as, vma* v) {
	if (v->prev) {
		v->prev->next = v->next;
	} else {
		as->vma_list = v->next;
	}
	if (v->next) {
		v->next->prev = v->prev;
	}
	if (as->vma_cache == v) {
		as->vma_cache = NULL;
	}
}

static void vma_destroy(vma* v) {
	if (v->ops && v->ops->close) {
		v->ops->close(v);
	}
	kfree(v);
}

// Splits the v at the address
// v keeps the lower part
// the new VMA is returned
static vma* vma_split(address_space_t* as, vma* v, uintptr_t addr) {
	vma* upper = vma_new(addr, v->end, v->prot, v->flags & ~VMM_MAP_GROWSDOWN, v->ops, v->priv, v->offset + (addr - v->start));
	if (!upper) {
		return NULL;
	}
	upper->grow_limit = v->grow_limit;
	v->end = addr;
	vma_link(as, upper);
	if (upper->ops && upper->ops->open) {
		upper->ops->open(upper);
	}
	return upper;
}

static bool range_free(address_space_t* as, uintptr_t start, uintptr_t end) {
	for (vma* v = as->vma_list; v; v = v->next) {
		if (v->start >= end) {
			break;
		}
		if (v->end > start) {
			return false;
		}
	}
	return true;
}

// First fit from the bottom up (from base)
// Keeps VMM_STACK_GUARD_GAP of space below any growable stack
// Returns 0 if no room
static uintptr_t find_gap(address_space_t* as, uintptr_t base, size_t len, size_t align) {
	uintptr_t cur = vmm_align_up(base, align);
	for (vma* v = as->vma_list; v; v = v->next) {
		if (v->end <= cur) {
			continue;
		}
		uintptr_t need_end = cur + len + ((v->flags & VMM_MAP_GROWSDOWN) ? VMM_STACK_GUARD_GAP : 0);
		if (need_end <= v->start && need_end > cur) {
			break;
		}
		cur = vmm_align_up(v->end, align);
	}
	if (cur + len < cur || cur + len > USER_VIRTUAL_END) {
		return 0;
	}
	return cur;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Unmapping
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/* Unmapping collects everything it frees into a batch, then flushes the TLBs once, THEN releases the frames.
 * Releasing first would let another CPU write through a stale translation into memory that (potentially) already belongs to someone else.
 */
#define BATCH_MAX 128
typedef struct unmap_batch {
	address_space_t* as;
	bool flush;
	struct {
		uint64_t phys;
		bool table;
	} items[BATCH_MAX];
	size_t count;
	uintptr_t lo;
	uintptr_t hi;
} unmap_batch;

static void batch_init(unmap_batch* b, address_space_t* as, bool flush) {
	b->as = as;
	b->flush = flush;
	b->count = 0;
	b->lo = (uintptr_t) -1;
	b->hi = 0;
}

static void batch_flush(unmap_batch* b) {
	if (!b->count) {
		return;
	}
	if (b->flush && b->hi > b->lo) {
		vmm_tlb_flush_range(b->as, b->lo, b->hi);
	}
	for (size_t i = 0; i < b->count; i++) {
		if (b->items[i].table) {
			vmm_free_table(b->items[i].phys);
		} else {
			vmm_free_user_frame(b->items[i].phys, 1);
		}
	}
	b->count = 0;
	b->lo = (uintptr_t) -1;
	b->hi = 0;
}

static void batch_add(unmap_batch* b, uint64_t phys, bool table, uintptr_t lo, uintptr_t hi) {
	if (b->count == BATCH_MAX) {
		batch_flush(b);
	}
	b->items[b->count].phys = phys;
	b->items[b->count].table = table;
	b->count++;
	if (lo < b->lo) {
		b->lo = lo;
	}
	if (hi > b->hi) {
		b->hi = hi;
	}
}

static bool table_empty(uint64_t* table) {
	for (int i = 0; i < TABLE_ENTRIES; i++) {
		if (table[i]) {
			return false;
		}
	}
	return true;
}

// Clears every mapping inside [start, end) below `table` and frees tables that end up empty
static void unmap_level(unmap_batch* b, uint64_t* table, int level, uintptr_t table_base, uintptr_t start, uintptr_t end) {
	uint64_t span = VMM_LVL_SIZE(level);
	for (int i = 0; i < TABLE_ENTRIES; i++) {
		uintptr_t e_start = table_base + (uint64_t) i * span;
		uintptr_t e_end = e_start + span;
		if (e_end <= start || e_start >= end) {
			continue;
		}
		uint64_t ent = table[i];
		if (!VMM_ENTRY_LIVE(ent)) {
			continue;
		}

		if (level == 1 || (level < 4 && (ent & BIT_SIZE))) {
			table[i] = 0;
			batch_add(b, vmm_entry_frame(ent, level), false, e_start, e_end);
		} else {
			uint64_t* child = vmm_table_ptr(ent & PTE_ADDR_4K);
			unmap_level(b, child, level - 1, e_start, start, end);
			if (table_empty(child)) {
				table[i] = 0;
				batch_add(b, ent & PTE_ADDR_4K, true, e_start, e_end);
			}
		}
	}
}

// as->lock held.
// `flush` is false only while tearing down an address space nobody is running on
static void unmap_range(address_space_t* as, uintptr_t start, uintptr_t end, bool flush) {
	unmap_batch b;
	batch_init(&b, as, flush);
	unmap_level(&b, as->pml4, 4, 0, start, end);
	batch_flush(&b);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// mmap / munmap / mprotect
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static bool huge_boundary_ok(vma* v, uintptr_t start, uintptr_t end) {
	if (!(v->flags & VMM_MAP_HUGE)) {
		return true;
	}
	if (start > v->start && (start & (PAGE_2MB_SIZE - 1))) {
		return false;
	}
	if (end < v->end && (end & (PAGE_2MB_SIZE - 1))) {
		return false;
	}
	return true;
}

// as->lock held
static int munmap_locked(address_space_t* as, uintptr_t start, uintptr_t end) {
	for (vma* v = as->vma_list; v; v = v->next) {
		if (v->start >= end) {
			break;
		}
		if (v->end > start && !huge_boundary_ok(v, start, end)) {
			return VMM_EINVAL;
		}
	}

	int result = VMM_OK;
	uintptr_t cleared_end = end;

	vma* v = as->vma_list;
	while (v && v->end <= start) {
		v = v->next;
	}
	while (v && v->start < end) {
		if (v->start < start) {
			vma* upper = vma_split(as, v, start);
			if (!upper) {
				result = VMM_ENOMEM;
				cleared_end = start;
				break;
			}
			v = upper;
		}
		if (v->end > end) {
			if (!vma_split(as, v, end)) {
				result = VMM_ENOMEM;
				cleared_end = v->start;
				break;
			}
		}
		vma* next = v->next;
		vma_unlink(as, v);
		vma_destroy(v);
		v = next;
	}

	if (cleared_end > start) {
		unmap_range(as, start, cleared_end, true);
	}
	return result;
}

int vmm_munmap(address_space_t* as, uintptr_t addr, size_t len) {
	if (!as || len == 0 || (addr & (PAGE_4KB_SIZE - 1))) {
		return VMM_EINVAL;
	}
	uintptr_t end = vmm_align_up(addr + len, PAGE_4KB_SIZE);
	if (end < addr || addr < USER_VIRTUAL_BASE || end > USER_VIRTUAL_END) {
		return VMM_EINVAL;
	}

	vmm_lock_acquire(&as->lock);
	int rc = munmap_locked(as, addr, end);
	vmm_lock_release(&as->lock);
	return rc;
}

static intptr_t do_mmap(address_space_t* as, uintptr_t addr, size_t len, uint32_t prot, uint32_t flags, const vma_ops* ops, void* priv, uint64_t offset, uintptr_t search_base) {
	if (!as || len == 0 || (prot & ~7u)) {
		return VMM_EINVAL;
	}
	bool huge = flags & VMM_MAP_HUGE;
	size_t gran = huge ? PAGE_2MB_SIZE : PAGE_4KB_SIZE;

	if (len + gran < len) { return VMM_EINVAL; }
	len = vmm_align_up(len, gran);

	if ((prot & VMM_PROT_WRITE) && (prot & VMM_PROT_EXEC)) return VMM_EACCES; // W^X
	if (ops && !ops->fault) return VMM_EINVAL;
	if ((flags & VMM_MAP_SHARED) && !ops) return VMM_EINVAL;
	if (huge && ops) return VMM_EINVAL;
	if ((flags & VMM_MAP_GROWSDOWN) && (ops || huge)) return VMM_EINVAL;

	bool fixed = flags & (VMM_MAP_FIXED | VMM_MAP_FIXED_NOREPLACE);
	if (fixed && (addr & (gran - 1))) return VMM_EINVAL;

	vmm_lock_acquire(&as->lock);

	uintptr_t start = 0;
	if (fixed) {
		if (addr < USER_VIRTUAL_BASE || addr + len < addr || addr + len > USER_VIRTUAL_END) {
			vmm_lock_release(&as->lock);
			return VMM_EINVAL;
		}
		if (!range_free(as, addr, addr + len)) {
			if (flags & VMM_MAP_FIXED_NOREPLACE) {
				vmm_lock_release(&as->lock);
				return VMM_EEXIST;
			}
			int rc = munmap_locked(as, addr, addr + len);
			if (rc != VMM_OK) {
				vmm_lock_release(&as->lock);
				return rc;
			}
		}
		start = addr;
	} else {
		if (addr) {
			uintptr_t hint = vmm_align_up(addr, gran);
			if (hint >= USER_VIRTUAL_BASE && hint + len > hint && hint + len <= USER_VIRTUAL_END && range_free(as, hint, hint + len)) {
				start = hint;
			}
		}
		if (!start) {
			start = find_gap(as, search_base, len, gran);
		}
		if (!start) {
			vmm_lock_release(&as->lock);
			return VMM_ENOMEM;
		}
	}

	vma* v = vma_new(start, start + len, prot, flags & ~(VMM_MAP_FIXED | VMM_MAP_FIXED_NOREPLACE | VMM_MAP_POPULATE), ops, priv, offset);
	if (!v) {
		vmm_lock_release(&as->lock);
		return VMM_ENOMEM;
	}
	if (flags & VMM_MAP_GROWSDOWN) {
		v->grow_limit = len > VMM_DEFAULT_STACK_LIMIT ? len : VMM_DEFAULT_STACK_LIMIT;
	}
	vma_link(as, v);
	if (ops && ops->open) {
		ops->open(v);
	}

	if (flags & VMM_MAP_POPULATE) {
		for (uintptr_t a = start; a < start + len; a += gran) {
			if (vmm_fault_in_locked(as, v, a, prot & VMM_PROT_WRITE) != VMM_OK) {
				munmap_locked(as, start, start + len);
				vmm_lock_release(&as->lock);
				return VMM_ENOMEM;
			}
		}
	}

	vmm_lock_release(&as->lock);
	return (intptr_t) start;
}

intptr_t vmm_mmap_ops(address_space_t* as, uintptr_t addr, size_t len, uint32_t prot, uint32_t flags, const vma_ops* ops, void* priv, uint64_t offset) { return do_mmap(as, addr, len, prot, flags, ops, priv, offset, USER_MMAP_BASE); }
intptr_t vmm_mmap(address_space_t* as, uintptr_t addr, size_t len, uint32_t prot, uint32_t flags) { return vmm_mmap_ops(as, addr, len, prot, flags, NULL, NULL, 0); }

// rewrites the permission bits of every present page in the range
// A page that caries BIT_COW stays read-only, the write fault will figure it out
static void protect_range(address_space_t* as, uintptr_t start, uintptr_t end, uint32_t prot) {
	uint64_t nx = vmm_feat.nx ? BIT_NX : 0;
	for (uintptr_t a = start; a < end;) {
		int level;
		uint64_t* entry = vmm_user_walk(as, a, &level);
		uint64_t span = VMM_LVL_SIZE(level);
		uintptr_t next = vmm_align_down(a, span) + span;

		if (entry && VMM_ENTRY_LIVE(*entry)) {
			uint64_t ent = *entry & ~(BIT_PRESENT | BIT_PROTNONE | BIT_WRITE | BIT_USR | BIT_NX);
			if (!(prot & (VMM_PROT_READ | VMM_PROT_WRITE | VMM_PROT_EXEC))) {
				ent |= BIT_PROTNONE;
			} else {
				ent |= BIT_PRESENT | BIT_USR;
				if ((prot & VMM_PROT_WRITE) && !(ent & BIT_COW)) {
					ent |= BIT_WRITE;
				}
				if (!(prot & VMM_PROT_EXEC)) {
					ent |= nx;
				}
			}
			*entry = ent;
		}
		a = next;
	}
}

int vmm_mprotect(address_space_t* as, uintptr_t addr, size_t len, uint32_t prot) {
	if (!as || len == 0 || (addr & (PAGE_4KB_SIZE - 1)) || (prot & ~7u)) {
		return VMM_EINVAL;
	}
	uintptr_t end = vmm_align_up(addr + len, PAGE_4KB_SIZE);
	if (end < addr || addr < USER_VIRTUAL_BASE || end > USER_VIRTUAL_END) {
		return VMM_EINVAL;
	}

	vmm_lock_acquire(&as->lock);

	// Check everything first so a failure doesn't leave half the range changed
	// no holes, W^X, huge boundaries
	uintptr_t covered = addr;
	for (vma* v = as->vma_list; v && covered < end; v = v->next) {
		if (v->end <= covered) {
			continue;
		}
		if (v->start > covered) {
			break; // Hole
		}
		if ((prot & VMM_PROT_WRITE) && (prot & VMM_PROT_EXEC)) {
			vmm_lock_release(&as->lock);
			return VMM_EACCES;
		}
		if (!huge_boundary_ok(v, addr, end)) {
			vmm_lock_release(&as->lock);
			return VMM_EINVAL;
		}
		covered = v->end;
	}
	if (covered < end) {
		vmm_lock_release(&as->lock);
		return VMM_ENOMEM;
	}

	// If a split runs out of memory halfway, the VMAs already changed must still get their page table entries updated.
	// Otherwise the VMA would say read-only while the PTEs stay writable.
	int result = VMM_OK;
	uintptr_t applied_end = addr;

	vma* v = as->vma_list;
	while (v && v->end <= addr) {
		v = v->next;
	}
	while (v && v->start < end) {
		if (v->start < addr) {
			vma* upper = vma_split(as, v, addr);
			if (!upper) {
				result = VMM_ENOMEM;
				break;
			}
			v = upper;
		}
		if (v->end > end) {
			if (!vma_split(as, v, end)) {
				result = VMM_ENOMEM;
				break;
			}
		}
		v->prot = prot;
		applied_end = v->end;
		v = v->next;
	}

	if (applied_end > addr) {
		protect_range(as, addr, applied_end, prot);
		vmm_tlb_flush_range(as, addr, applied_end); // Tightened permissions have to reach every CPU
	}
	vmm_lock_release(&as->lock);
	return result;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Stack Growth
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// `v` is the lowest VMA above addr and has VMM_MAP_GROWSDOWN.
// as->lock held.
int vmm_stack_grow(address_space_t* as, vma* v, uintptr_t addr) {
	(void) as;
	uintptr_t new_start = vmm_align_down(addr, PAGE_4KB_SIZE);
	if (new_start < USER_VIRTUAL_BASE || v->end - new_start > v->grow_limit) {
		return VMM_EFAULT;
	}
	// Don't grow into the guard gap above whatever sits below.
	if (v->prev && v->prev->end + VMM_STACK_GUARD_GAP > new_start) {
		return VMM_EFAULT;
	}
	v->start = new_start;
	return VMM_OK;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Actual Address Spaces
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

address_space_t* vmm_create_address_space(void) {
	address_space_t* as = (address_space_t*) kcalloc(1, sizeof(address_space_t));
	if (!as) {
		return NULL;
	}
	uint64_t phys = vmm_alloc_table();
	if (!phys) {
		kfree(as);
		return NULL;
	}

	as->pml4_phys = phys;
	as->pml4 = vmm_table_ptr(phys);

	// The kernel half is shared
	as->pml4[0] = vmm_kernel_pml4[0]; // Low identity map, supervisor only
	for (int i = 256; i < TABLE_ENTRIES; i++) {
		as->pml4[i] = vmm_kernel_pml4[i];
	}

	as->pcid = vmm_pcid_alloc();
	as->refcount = 1;
	vmm_lock_init(&as->lock);

	vmm_lock_acquire(&as_list_lock);
	as->id = next_as_id++;
	as->all_next = as_list;
	as_list = as;
	vmm_lock_release(&as_list_lock);

	VMM_STAT(as_created);
	return as;
}

void vmm_destroy_address_space(address_space_t* as) {
	if (!as || as->is_kernel) return;
	if (__atomic_sub_fetch(&as->refcount, 1, __ATOMIC_ACQ_REL) != 0) return;

	// Never tear down the tables we're actively on
	if (vmm_current_as() == as) vmm_switch_address_space(&vmm_kernel_as);

	// Nobody holds a reference
	// Another CPU could (potentially) have these tables still loaded, which is a conflict that shouldn't happen
	if (vmm_as_loaded_on_other_cpu(as)) {
		panic_s("[VMM] Address space is still loaded on another CPU (reference dropped while a task was still running in it).");
	}

	vmm_lock_acquire(&as_list_lock);
	for (address_space_t** pp = &as_list; *pp; pp = &(*pp)->all_next) {
		if (*pp == as) {
			*pp = as->all_next;
			break;
		}
	}
	vmm_lock_release(&as_list_lock);

	// Nothing is running in it, so no point flushing page by page
	// One context flush at the end covers everything
	vmm_lock_acquire(&as->lock);
	while (as->vma_list) {
		vma* v = as->vma_list;
		vma_unlink(as, v);
		vma_destroy(v);
	}
	unmap_range(as, USER_VIRTUAL_BASE, USER_VIRTUAL_END, false);
	vmm_lock_release(&as->lock);

	// Cached translations tagged with our PCID could still exist on any CPU that ever ran us
	// Kill them before the PCID is reused
	vmm_tlb_flush_context(as);
	vmm_pcid_free(as->pcid);

	vmm_free_table(as->pml4_phys);
	kfree(as);
	VMM_STAT(as_destroyed);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Fork
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

address_space_t* vmm_fork_address_space(address_space_t* parent) {
	address_space_t* child = vmm_create_address_space();
	if (!child) {
		return NULL;
	}

	bool ok = true;
	vmm_lock_acquire(&parent->lock);

	for (vma* v = parent->vma_list; v && ok; v = v->next) {
		vma* nv = vma_new(v->start, v->end, v->prot, v->flags, v->ops, v->priv, v->offset);
		if (!nv) {
			ok = false;
			break;
		}
		nv->grow_limit = v->grow_limit;
		vma_link(child, nv);
		if (nv->ops && nv->ops->open) {
			nv->ops->open(nv);
		}

		bool shared = v->flags & VMM_MAP_SHARED;

		for (uintptr_t a = v->start; a < v->end && ok;) {
			int level;
			uint64_t* pe = vmm_user_walk(parent, a, &level);
			uint64_t span = VMM_LVL_SIZE(level);
			uintptr_t next = vmm_align_down(a, span) + span;

			if (pe && VMM_ENTRY_LIVE(*pe)) {
				if (!shared) {
					// Both sides get a read-only COW view
					// Even if the VMA isn't writable right now it could be after an mprotect
					*pe = (*pe & ~BIT_WRITE) | BIT_COW;
				}
				uint64_t ent = *pe;
				uint64_t frame = vmm_entry_frame(ent, level);
				uint64_t flags = (ent & ~frame) & ~(BIT_ACCESS | BIT_DIRTY);

				pmm_ref_inc(frame);
				if (vmm_pt_map(child->pml4, vmm_align_down(a, span), frame, flags, level) != VMM_OK) {
					pmm_ref_dec(frame); // Child doesn't hold it
					ok = false;
				}
			}
			a = next;
		}
	}

	// The parent's pages just became read-only, every CPU running it has to notice before it writes again
	vmm_tlb_flush_range(parent, USER_VIRTUAL_BASE, USER_VIRTUAL_END);
	vmm_lock_release(&parent->lock);

	if (!ok) {
		vmm_destroy_address_space(child);
		return NULL;
	}
	return child;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// User Access
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

int user_access_ok(const void* ptr, size_t len) {
	uintptr_t addr = (uintptr_t) ptr;
	uintptr_t end = addr + len;
	return end >= addr && addr >= USER_VIRTUAL_BASE && end <= USER_VIRTUAL_END;
}

void vmm_user_access_begin(void) {
	if (vmm_smap_on) {
		__asm__ volatile("stac" : : : "memory", "cc");
	}
}

void vmm_user_access_end(void) {
	if (vmm_smap_on) {
		__asm__ volatile("clac" : : : "memory", "cc");
	}
}

size_t copy_from_user(void* dst, const void* user_src, size_t n) {
	if (!user_access_ok(user_src, n)) {
		return n;
	}
	return vmm_uaccess_copy_raw(dst, user_src, n);
}

size_t copy_to_user(void* user_dst, const void* src, size_t n) {
	if (!user_access_ok(user_dst, n)) {
		return n;
	}
	return vmm_uaccess_copy_raw(user_dst, src, n);
}

size_t clear_user(void* user_dst, size_t n) {
	if (!user_access_ok(user_dst, n)) {
		return n;
	}
	return vmm_uaccess_clear_raw(user_dst, n);
}

long strncpy_from_user(char* dst, const char* user_src, size_t max) {
	if (max == 0) {
		return 0;
	}
	size_t copied = 0;
	while (copied < max) {
		uintptr_t src = (uintptr_t) user_src + copied;
		// One page at a time, so a string ending right before an unmapped page doesn't fail the whole copy.
		size_t chunk = PAGE_4KB_SIZE - (src & (PAGE_4KB_SIZE - 1));
		if (chunk > max - copied) {
			chunk = max - copied;
		}
		size_t left = copy_from_user(dst + copied, (const void*) src, chunk);
		size_t got = chunk - left;
		for (size_t i = 0; i < got; i++) {
			if (dst[copied + i] == '\0') {
				return (long) (copied + i);
			}
		}
		if (left) {
			return VMM_EFAULT;
		}
		copied += chunk;
	}
	return (long) max; // No terminator within max bytes. dst is not NUL terminated.
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Misc, mostly for inspection or cross-access
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

int vmm_query_page(address_space_t* as, uintptr_t addr, vmm_page_info_t* out) {
	if (!as || !out) {
		return 0;
	}
	vmm_lock_acquire(&as->lock);
	int level;
	uint64_t* e = vmm_pt_walk(as->pml4, addr, &level);
	int present = e && VMM_ENTRY_LIVE(*e);
	if (present) {
		uint64_t ent = *e;
		uint64_t frame = vmm_entry_frame(ent, level);
		out->phys = frame;
		out->level = (uint32_t) level;
		out->flags =
			((ent & BIT_PRESENT) ? VMM_PAGE_PRESENT : VMM_PAGE_NONE)
			| ((ent & BIT_WRITE) ? VMM_PAGE_WRITE : 0)
			| ((ent & BIT_USR) ? VMM_PAGE_USER : 0)
			| ((ent & BIT_NX) ? VMM_PAGE_NX : 0)
			| ((ent & BIT_COW) ? VMM_PAGE_COW : 0)
			| (level > 1 ? VMM_PAGE_HUGE : 0);
		out->refs = pmm_ref_get(frame);
	}
	vmm_lock_release(&as->lock);
	return present;
}

int vmm_query_vma(address_space_t* as, uintptr_t addr, vmm_vma_info_t* out) {
	vma copy;
	if (!as || !out || !vmm_copy_vma(as, addr, &copy)) {
		return 0;
	}
	out->start = copy.start;
	out->end = copy.end;
	out->prot = copy.prot;
	out->flags = copy.flags;
	return 1;
}

size_t vmm_vma_count(address_space_t* as) {
	size_t n = 0;
	vmm_lock_acquire(&as->lock);
	for (vma* v = as->vma_list; v; v = v->next) {
		n++;
	}
	vmm_lock_release(&as->lock);
	return n;
}

#define AS_PF_WRITE (1u << 1)
#define AS_PF_USER  (1u << 2)

static int as_access(address_space_t* as, uintptr_t addr, void* buf, size_t n, bool write) {
	if (!as) {
		return VMM_EINVAL;
	}
	if (n == 0) {
		return VMM_OK;
	}
	if (addr + n < addr || addr < USER_VIRTUAL_BASE || addr + n > USER_VIRTUAL_END) {
		return VMM_EFAULT;
	}

	uint8_t* p = (uint8_t*) buf;
	while (n) {
		uintptr_t page = vmm_align_down(addr, PAGE_4KB_SIZE);
		size_t chunk = PAGE_4KB_SIZE - (addr - page);
		if (chunk > n) {
			chunk = n;
		}

		for (int tries = 0;; tries++) {
			// Same path a real user access takes
			// We find the VMA, checks the permission, allocate, breaks COW, grow the stack
			int rc = vmm_handle_user_fault(as, addr, AS_PF_USER | (write ? AS_PF_WRITE : 0));
			if (rc != VMM_OK) {
				return rc;
			}

			// The lock was dropped since
			// Another thread may have unmapped the page again, so look once more and copy under the lock
			vmm_lock_acquire(&as->lock);
			int level;
			uint64_t* e = vmm_pt_walk(as->pml4, addr, &level);
			if (e && (*e & BIT_PRESENT) && (!write || (*e & BIT_WRITE))) {
				uint64_t span = VMM_LVL_SIZE(level);
				uint64_t phys = vmm_entry_frame(*e, level) + (addr & (span - 1));
				if (write) {
					vmm_memcopy((void*) vmm_phys_to_virt_addr(phys), p, chunk);
				} else {
					vmm_memcopy(p, (const void*) vmm_phys_to_virt_addr(phys), chunk);
				}
				vmm_lock_release(&as->lock);
				break;
			}
			vmm_lock_release(&as->lock);
			if (tries >= 8) {
				return VMM_EFAULT;
			}
		}

		addr += chunk;
		p += chunk;
		n -= chunk;
	}
	return VMM_OK;
}

int vmm_write_address_space(address_space_t* as, uintptr_t addr, const void* src, size_t n) {
	return as_access(as, addr, (void*) src, n, true);
}

int vmm_read_address_space(address_space_t* as, uintptr_t addr, void* dst, size_t n) {
	return as_access(as, addr, dst, n, false);
}

typedef struct c_backing {
	vmm_backing_fault_fn fault;
	vmm_backing_event_fn open;
	vmm_backing_event_fn close;
	void* user;
	uint32_t refs;
} c_backing;

static void c_backing_unref(c_backing* b) {
	if (__atomic_sub_fetch(&b->refs, 1, __ATOMIC_ACQ_REL) == 0) {
		kfree(b);
	}
}

static uintptr_t c_backing_fault(vma* v, uint64_t file_offset, bool write) {
	c_backing* b = (c_backing*) v->priv;
	return b->fault(b->user, file_offset, write);
}

static void c_backing_open(vma* v) {
	c_backing* b = (c_backing*) v->priv;
	__atomic_add_fetch(&b->refs, 1, __ATOMIC_ACQ_REL);
	if (b->open) {
		b->open(b->user);
	}
}

static void c_backing_close(vma* v) {
	c_backing* b = (c_backing*) v->priv;
	if (b->close) {
		b->close(b->user);
	}
	c_backing_unref(b);
}

static const vma_ops c_backing_ops = {c_backing_fault, c_backing_open, c_backing_close};

intptr_t vmm_mmap_backed(address_space_t* as, uintptr_t addr, size_t len, uint32_t prot, uint32_t flags, vmm_backing_fault_fn fault, vmm_backing_event_fn open, vmm_backing_event_fn close, void* priv, uint64_t offset) {
	if (!fault) {
		return VMM_EINVAL;
	}
	c_backing* b = (c_backing*) kcalloc(1, sizeof(c_backing));
	if (!b) {
		return VMM_ENOMEM;
	}
	b->fault = fault;
	b->open = open;
	b->close = close;
	b->user = priv;
	b->refs = 1; // Ours, so a failing mmap can't free it from under us

	intptr_t r = vmm_mmap_ops(as, addr, len, prot, flags, &c_backing_ops, b, offset);
	c_backing_unref(b);
	return r;
}

uintptr_t vmm_frame_alloc(unsigned order) {
	return pmm_alloc_pages((uint8_t) order);
}

void vmm_frame_put(uintptr_t phys) {
	pmm_put(phys);
}

uint32_t vmm_frame_refs(uintptr_t phys) {
	return pmm_ref_get(phys);
}

uint32_t vmm_feature_flags(void) {
	return (vmm_feat.nx ? VMM_FEAT_NX : 0) | (vmm_feat.pat ? VMM_FEAT_PAT : 0) | (vmm_feat.pge ? VMM_FEAT_PGE : 0) | (vmm_use_pcid ? VMM_FEAT_PCID : 0) | (vmm_feat.smep ? VMM_FEAT_SMEP : 0) | (vmm_smap_on ? VMM_FEAT_SMAP : 0) | (vmm_feat.gb_pages ? VMM_FEAT_GB : 0);
}

void vmm_stats_get(vmm_stats_snapshot_t* out) {
	out->faults_total = __atomic_load_n(&vmm_stat.faults_total, __ATOMIC_RELAXED);
	out->faults_demand = __atomic_load_n(&vmm_stat.faults_demand, __ATOMIC_RELAXED);
	out->faults_cow = __atomic_load_n(&vmm_stat.faults_cow, __ATOMIC_RELAXED);
	out->faults_spurious = __atomic_load_n(&vmm_stat.faults_spurious, __ATOMIC_RELAXED);
	out->faults_stack_grow = __atomic_load_n(&vmm_stat.faults_stack_grow, __ATOMIC_RELAXED);
	out->faults_user_fatal = __atomic_load_n(&vmm_stat.faults_user_fatal, __ATOMIC_RELAXED);
	out->faults_uaccess_fixup = __atomic_load_n(&vmm_stat.faults_uaccess_fixup, __ATOMIC_RELAXED);
	out->tlb_local_flushes = __atomic_load_n(&vmm_stat.tlb_local_flushes, __ATOMIC_RELAXED);
	out->tlb_shootdowns = __atomic_load_n(&vmm_stat.tlb_shootdowns, __ATOMIC_RELAXED);
	out->tlb_ipis_sent = __atomic_load_n(&vmm_stat.tlb_ipis_sent, __ATOMIC_RELAXED);
}
