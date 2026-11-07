#include <drivers/serial.h>
#include <memory/virtual_mem.h>
#include <memory/vmm_internal.h>
#include <panic.h>
#include <scheduler/cpu.h>
#include <string.h>
#include <wallos_attributes.h>
#include <x86_64/lapic.h>

/* TLB Shootdowns are very annoying.
 * Every CPU has a cache of virtual to physical mappings, that it never checks against the real table unless told to.
 * When we edit a page table in a way that others need to know about, we have to tell them to refresh their TLB cache.
 * We need to tell others when we:
 *     - Remove a mapping
 *     - "Tighten" a mapping (like changing from writeable to only readable)
 * We don't need to do it for loosening a mapping (like present to nonpresent), it would cause a page fault that the handler will resolve anyway.
 * Adding a mapping will require the CPU to pull it from the page table anyway.
 *
 * Each address space keeps a mask of cpus that might have it's translations cached.
 * Without PCID, cr3 loads flush non-global translations, meaning that when they switch to another process, the cache gets flushed already.
 * This means we don't really need to worry about shooting them down in other CPUs.
 *
 * With PCID, each address space is tagged with a PCID, and the CPUs can cache things that are switched away from.
 * INVPCID can invalidate other CPUs TLB for that PCID without having to switch to that address space.
 *
 * Kernel mappings are global, meaning all CPUs will have translations for it cached, so every CPU has to be invalidated. (this is the annoying one)
 *
 *
 * General design (because I will forget):
 * Only one request is sent at a time.
 * The sender flushes it's own TLB, fills in the request of what to flush, and sets a pending bit for each target CPU.
 * It then sends an IPI to each target and spins until all the pending bits are clear.
 *
 * The targets receive the IPI, flush what was requested, and then clear their pending bit.
 */

// this lets us not have to decode CR3 (and keep it aligned to cache line size)
struct vmm_percpu {
	address_space_t* current; //< address space loaded in cr3
} WALLOS_ALIGNED(64);

// There's a good chance we'll get here for SMP bringup, since that's the last thing done.
// Everything assumes we have at least 1 CPU (duh), but we need to know if we're allowed to ask the scheduler things or not
static bool smp_active = false; // We need to keep track of if the scheduler is up or not. If not, there's one CPU.
static struct vmm_percpu percpu[VMM_MAX_CPUS]; // easy lookup for each CPU to keep track of it's current address space
static uint64_t online_mask[VMM_MAX_CPUS / 64] = {1}; // CPUs that can answer shootdown IPIs. defaults only cpu 0 to online

// Bounds checking in case we end up with more CPUs than the VMM can support
static inline uint32_t checked_index(uint32_t index) {
	if (index >= VMM_MAX_CPUS) {
		panic_s("[VMM] CPU index is above VMM_MAX_CPUS.");
	}
	return index;
}

// Get this CPUs index. Before SMP we know it's the BSP.
static inline uint32_t cpu_index(void) { return smp_active ? checked_index(cpu_current()->id) : 0; }
void vmm_enable_smp(void) { smp_active = true; }

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// CPU Mask Stuff
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static inline void mask_set(uint64_t* mask, uint32_t cpu) { __atomic_fetch_or(&mask[cpu >> 6], 1ULL << (cpu & 63), __ATOMIC_SEQ_CST); }
static inline void mask_clear(uint64_t* mask, uint32_t cpu) { __atomic_fetch_and(&mask[cpu >> 6], ~(1ULL << (cpu & 63)), __ATOMIC_SEQ_CST); }
static inline bool mask_test(const uint64_t* mask, uint32_t cpu) { return __atomic_load_n(&mask[cpu >> 6], __ATOMIC_SEQ_CST) & (1ULL << (cpu & 63)); }

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Local invalidation
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

#define FLUSH_RANGE_PAGES_MAX 32 // Past this a full flush is cheaper than invalidating page by page

// Throws away every tranlsation on this cpu
// Meant for kernel space changes mostly
static void flush_all_global(void) {
	if (vmm_feat.pge) {
		// Any change to CR4.PGE flushes the entire TLB, global entries included
		// Toggle it off and back on
		uint64_t cr4 = vmm_read_cr4();
		vmm_write_cr4(cr4 & ~(1ULL << 7));
		vmm_write_cr4(cr4);
	} else if (vmm_use_pcid) {
		// No global pages
		// kernel entries are tagged per PCID, so every PCID has to be dropped
		vmm_invpcid(2, 0, 0);
	} else {
		// No globals and no PCIDs
		// a plain CR3 reload flushes everything
		vmm_write_cr3(vmm_read_cr3());
	}
}

// Handles invalidation for THIS cpu.
// context means we drop everything cached for `as`'s PCID
static void apply_flush(uint32_t cpu, address_space_t* as, uintptr_t start, uintptr_t end, bool context) {
	VMM_STAT(tlb_local_flushes);
	uint64_t pages = (end - start) >> 12;

	// as == NULL is a kernel mapping
	if (!as) {
		// Kernel mappings are global entries
		// INVLPG removes them regardless of which PCID is loaded.
		if (context || pages > FLUSH_RANGE_PAGES_MAX) {
			flush_all_global();
		} else if (!vmm_feat.pge && vmm_use_pcid) {
			vmm_invpcid(2, 0, 0); // Not global, so they're tagged per PCID
		} else {
			for (uintptr_t a = start; a < end; a += PAGE_4KB_SIZE) {
				vmm_invlpg(a);
			}
		}
		return;
	}

	if (context) {
		// Used right before a PCID is recycled
		// Without a PCID there's nothing tagged to drop (the CR3 loads already did it)
		if (vmm_use_pcid && as->pcid) {
			vmm_invpcid(1, as->pcid, 0);
		}
		return;
	}

	if (percpu[cpu].current == as) {
		// The address space is loaded right here, so plain INVLPG hits the right entries.
		if (pages > FLUSH_RANGE_PAGES_MAX) {
			// CR3 reads never return bit 63, so writing the value straight back reloads CR3 WITHOUT the "no flush" bit.
			// That drops this PCID's non-global entries.
			vmm_write_cr3(vmm_read_cr3() & ~(1ULL << 63));
		} else {
			for (uintptr_t a = start; a < end; a += PAGE_4KB_SIZE) {
				vmm_invlpg(a);
			}
		}
	} else if (vmm_use_pcid && as->pcid) {
		// Not loaded here, but its translations may still be cached under its PCID
		// INVLPG would only look at the PCID that IS loaded
		if (pages > FLUSH_RANGE_PAGES_MAX) {
			vmm_invpcid(1, as->pcid, 0);
		} else {
			for (uintptr_t a = start; a < end; a += PAGE_4KB_SIZE) {
				vmm_invpcid(0, as->pcid, a);
			}
		}
	}
	// Otherwise it isn't loaded here and has no PCID tag. The next switch to it flushes anyway.
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Shootdown
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/* An AP calls this once its IDT and LAPIC are ready */
void vmm_cpu_online(void) {
	// Anything this CPU cached while it wasn't a shootdown target may be stale by now, so start from a clean TLB
	flush_all_global();
	mask_set(online_mask, cpu_index());
}

// The lock for the shootdowns to make sure only one shootdown is happening at a time
static vmm_lock shoot_lock;
// The shootdown request
static struct {
	address_space_t* as;
	uintptr_t start;
	uintptr_t end;
	bool context;
} shoot_req;
static volatile uint64_t shoot_pending[VMM_MAX_CPUS / 64]; // Bit set means "this CPU still has to flush"

// If a shootdown is waiting for this CPU, do it, and acknowledge
// Does nothing if nothing is pending
static void service(uint32_t cpu) {
	uint64_t bit = 1ULL << (cpu & 63);
	if (!(__atomic_load_n(&shoot_pending[cpu >> 6], __ATOMIC_ACQUIRE) & bit)) {
		return;
	}
	apply_flush(cpu, shoot_req.as, shoot_req.start, shoot_req.end, shoot_req.context);
	__atomic_fetch_and(&shoot_pending[cpu >> 6], ~bit, __ATOMIC_RELEASE);
}

// Called from the spin loop of every VMM lock so a CPU that's waiting on one (with interrupts off) still answers shootdowns
void vmm_tlb_poll(void) {
	if (smp_active) {
		service(cpu_index());
	}
}

// IPI handler for the shootdown vector
WALLOS_INTERRUPT_HANDLER void vmm_tlb_shootdown_handler(struct interrupt_regs* regs) {
	(void) regs;
	service(checked_index(cpu_current()->id));
	lapic_write(LAPIC_EOI, 0);
}

// Flushes this CPU and on every other CPU that may have the translations cached
// Waits for the others to finish before returning
static void broadcast(address_space_t* as, uintptr_t start, uintptr_t end, bool context) {
	if (!context && end <= start) return;


	__atomic_thread_fence(__ATOMIC_SEQ_CST);

	uint64_t irq = vmm_irq_save();
	uint32_t self = cpu_index();

	// ourself first
	apply_flush(self, as, start, end, context);

	uint32_t count = smp_active ? cpu_count() : 1;
	if (count > VMM_MAX_CPUS) count = VMM_MAX_CPUS;

	// Figure out who else needs to flush
	uint64_t targets[VMM_MAX_CPUS / 64];
	memset(targets, 0, sizeof(targets));
	uint32_t target_count = 0;
	for (uint32_t cpu = 0; cpu < count; cpu++) {
		if (cpu == self) continue;

		if (!mask_test(online_mask, cpu)) {
			continue;
		}
		if (!as || mask_test(as->cpu_mask, cpu)) {
			targets[cpu >> 6] |= 1ULL << (cpu & 63);
			target_count++;
		}
	}

	// Often others won't have cached it
	// if they havent, no need for an IPI
	if (target_count) {
		vmm_lock_acquire(&shoot_lock);

		// Fill request and targets
		shoot_req.as = as;
		shoot_req.start = start;
		shoot_req.end = end;
		shoot_req.context = context;
		for (uint32_t w = 0; w < (count + 63) / 64; w++) {
			__atomic_store_n(&shoot_pending[w], targets[w], __ATOMIC_RELEASE);
		}

		// send the ipis
		for (uint32_t cpu = 0; cpu < count; cpu++) {
			if (targets[cpu >> 6] & (1ULL << (cpu & 63))) {
				arch_send_ipi(cpu, VMM_TLB_VECTOR);
				VMM_STAT(tlb_ipis_sent);
			}
		}
		VMM_STAT(tlb_shootdowns);

		// wait for acknowledgement
		for (;;) {
			bool done = true;
			for (uint32_t w = 0; w < (count + 63) / 64; w++) {
				if (__atomic_load_n(&shoot_pending[w], __ATOMIC_ACQUIRE)) {
					done = false;
				}
			}
			if (done) {
				break;
			}
			// There's a chance someone else is waiting on us
			// Our bit set atomically may have been a race with another that got it before us
			service(self);
			__asm__ volatile("pause");
		}
		vmm_lock_release(&shoot_lock);
	}
	vmm_irq_restore(irq);
}

void vmm_tlb_flush_range(address_space_t* as, uintptr_t start, uintptr_t end) { broadcast(as, vmm_align_down(start, PAGE_4KB_SIZE), vmm_align_up(end, PAGE_4KB_SIZE), false); }
void vmm_tlb_flush_context(address_space_t* as) { broadcast(as, 0, 0, true); }

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// PCIDs
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// PCID 0 means both the kernel and "none"
// 1..4095 go to address spaces
// If we run out every switch to them requires a flush (set them to 0)
static uint64_t pcid_bitmap[4096 / 64];
static vmm_lock pcid_lock;

uint16_t vmm_pcid_alloc(void) {
	if (!vmm_use_pcid) return 0;


	/* There's a lot of bitmap shenanigans below, just trust me it works correctly */

	vmm_lock_acquire(&pcid_lock);
	pcid_bitmap[0] |= 1; // 0 is reserved
	for (int w = 0; w < 4096 / 64; w++) {
		if (pcid_bitmap[w] != ~0ULL) {
			// Lowest zero bit = first free PCID in this word
			int bit = __builtin_ctzll(~pcid_bitmap[w]);
			pcid_bitmap[w] |= 1ULL << bit;
			vmm_lock_release(&pcid_lock);
			return (uint16_t) (w * 64 + bit);
		}
	}
	vmm_lock_release(&pcid_lock);
	return 0;
}

void vmm_pcid_free(uint16_t pcid) {
	if (!pcid) {
		return;
	}
	vmm_lock_acquire(&pcid_lock);
	pcid_bitmap[pcid >> 6] &= ~(1ULL << (pcid & 63));
	vmm_lock_release(&pcid_lock);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Address space switching
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

void vmm_percpu_init(void) {
	uint32_t cpu = cpu_index();
	percpu[cpu].current = &vmm_kernel_as;
	mask_set(vmm_kernel_as.cpu_mask, cpu);
}

// Debug check for vmm_destroy_address_space
// Is any CPU other than this one still running `as`?
bool vmm_as_loaded_on_other_cpu(address_space_t* as) {
	uint32_t self = cpu_index();
	uint32_t count = smp_active ? cpu_count() : 1;
	if (count > VMM_MAX_CPUS) {
		count = VMM_MAX_CPUS;
	}
	for (uint32_t cpu = 0; cpu < count; cpu++) {
		if (cpu != self && __atomic_load_n(&percpu[cpu].current, __ATOMIC_ACQUIRE) == as) {
			return true;
		}
	}
	return false;
}

address_space_t* vmm_current_as(void) {
	address_space_t* cur = percpu[cpu_index()].current;
	return cur ? cur : &vmm_kernel_as;
}

uint64_t* vmm_current_root(void) { return vmm_current_as()->pml4; }

address_space_t* vmm_current_address_space(void) { return vmm_current_as(); }

// Loads the address space to this CPU
void vmm_switch_address_space(address_space_t* as) {
	if (!as) as = &vmm_kernel_as;


	uint64_t irq = vmm_irq_save();
	uint32_t cpu = cpu_index();
	address_space_t* old = percpu[cpu].current;

	if (old != as) {
		// Make sure we're apart of the mask for this
		mask_set(as->cpu_mask, cpu);

		uint64_t cr3 = as->pml4_phys;
		if (vmm_use_pcid && as->pcid) {
			cr3 |= as->pcid | (1ULL << 63); // Bit 63: keep this PCID's cached translations
		}

		vmm_write_cr3(cr3);
		percpu[cpu].current = as;

		// Without PCID the load above threw away every non-global entry, so the old address space isn't cached here anymore
		// With PCID its entries are still sitting in the TLB under its tag, so it must stay in the mask
		if (old && !vmm_use_pcid) {
			mask_clear(old->cpu_mask, cpu);
		}
	}
	vmm_irq_restore(irq);
}