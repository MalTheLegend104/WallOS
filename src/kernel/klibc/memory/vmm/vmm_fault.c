/* The page fault handler.
 *
 * Page faults are how most of the VMM actually works:
 *   - Demand paging: mmap only records a VMA. The first touch of a page faults, and we allocate (zeroed) memory then.
 *   - Copy-on-write: fork leaves frames shared and read-only (BIT_COW). The first write faults, and we copy (or take over the frame if we're the last user).
 *   - Stack growth: a fault just below a VMM_MAP_GROWSDOWN VMA extends it, as long as the guard gap and size limit allow.
 *   - copy_to/from_user: a fault inside the fixup-able copy instructions that can't be resolved returns "bytes left" instead of crashing.
 *
 * Error code bits (pushed by the CPU): 0 P (page was present), 1 W (write), 2 U (from ring 3), 3 RSVD (reserved bit set in tables), 4 I (instruction fetch).
 */

#include <drivers/serial.h>
#include <memory/virtual_mem.h>
#include <memory/vmm_internal.h>
#include <panic.h>
#include <string.h>
#include <system/idt.h>
#include <wallos_attributes.h>

#define PF_PRESENT (1u << 0)
#define PF_WRITE   (1u << 1)
#define PF_USER    (1u << 2)
#define PF_RSVD    (1u << 3)
#define PF_INSTR   (1u << 4)
#define PF_PK      (1u << 5)
#define PF_SS      (1u << 6)

#define RFLAGS_AC (1ULL << 18)

static vmm_user_fault_cb user_fault_cb = NULL;
void vmm_set_user_fault_callback(vmm_user_fault_cb cb) { user_fault_cb = cb; }

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Fault Resolution
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// The write fault hit a present page that carries BIT_COW
// The as->lock is held
static int break_cow(address_space_t* as, uintptr_t page, uint64_t* entry, int level) {
	uint64_t ent = *entry;
	uint64_t span = VMM_LVL_SIZE(level);
	uint64_t frame = vmm_entry_frame(ent, level);
	VMM_STAT(faults_cow);

	// Only one reference left, meaning nobody else can see this frame, so it's ours now. Nothing to copy.
	// Only this address space's lock can add a reference (via fork), and we hold it, so the count can't go back up here
	if (pmm_ref_get(frame) == 1) {
		*entry = (ent | BIT_WRITE) & ~BIT_COW;
		return VMM_OK;
	}

	uint64_t copy = pmm_alloc_pages(level == 2 ? 9 : 0);
	if (!copy) {
		return VMM_ENOMEM;
	}
	vmm_memcopy((void*) vmm_phys_to_virt_addr(copy), (const void*) vmm_phys_to_virt_addr(frame), span);

	uint64_t flags = ((ent & ~frame) & ~(BIT_COW | BIT_ACCESS | BIT_DIRTY)) | BIT_WRITE;
	*entry = copy | flags;

	// Other CPUs running this address space may still have the old frame cached
	vmm_tlb_flush_range(as, page, page + span);
	pmm_put(frame);
	return VMM_OK;
}

// Makes the page containing addr usable
// This takes as->lock
int vmm_fault_in_locked(address_space_t* as, vma* v, uintptr_t addr, bool write) {
	bool huge = v->flags & VMM_MAP_HUGE;
	uint64_t page_size = huge ? PAGE_2MB_SIZE : PAGE_4KB_SIZE;
	uintptr_t page = vmm_align_down(addr, page_size);

	int level;
	uint64_t* entry = vmm_user_walk(as, page, &level);

	if (entry && (*entry & BIT_PROTNONE)) {
		// The VMA allows access but the entry still says PROT_NONE
		// mprotect() restores every entry, so this shouldn't happen
		// We it instead of mapping a second page on top of the one the entry holds.
		uint64_t ent = *entry;
		*entry = (ent & ~(BIT_PROTNONE | BIT_WRITE | BIT_USR | BIT_NX)) | vmm_user_leaf_flags(v->prot, ent & BIT_COW);
		return VMM_OK;
	}

	if (entry && (*entry & BIT_PRESENT)) {
		uint64_t ent = *entry;

		if (write) {
			if (!(v->prot & VMM_PROT_WRITE)) {
				return VMM_EFAULT;
			}
			if (ent & BIT_COW) {
				return break_cow(as, page, entry, level);
			}
		}

		// Mapped already, this fault was probably a stale TLB entry or the VMA was wrong
		uint64_t mask = BIT_WRITE | BIT_USR | BIT_NX;
		uint64_t want = vmm_user_leaf_flags(v->prot, ent & BIT_COW) & mask;
		if ((ent & mask) != want) {
			*entry = (ent & ~mask) | want;
		}
		VMM_STAT(faults_spurious);
		return VMM_OK;
	}

	// Not mapped, we need to bring a page in
	uint64_t phys;
	uint64_t flags;
	if (v->ops) {
		phys = v->ops->fault(v, v->offset + (page - v->start), write);
		if (!phys) {
			return VMM_EFAULT;
		}
		// Private mappings get a read-only COW view of the backing page
		// shared ones write through
		flags = vmm_user_leaf_flags(v->prot, !(v->flags & VMM_MAP_SHARED));
	} else {
		phys = pmm_alloc_pages(huge ? 9 : 0);
		if (!phys) {
			return VMM_ENOMEM;
		}
		// The physical page could have been used before, and it's about to be user memory
		vmm_memzero((void*) vmm_phys_to_virt_addr(phys), page_size);
		flags = vmm_user_leaf_flags(v->prot, false);
	}
	if (huge) {
		flags |= BIT_SIZE;
	}

	int rc = vmm_pt_map(as->pml4, page, phys, flags, huge ? 2 : 1);
	if (rc != VMM_OK) {
		pmm_put(phys);
		return rc;
	}
	VMM_STAT(faults_demand);
	return VMM_OK;
}

int vmm_handle_user_fault(address_space_t* as, uintptr_t addr, uint64_t error) {
	if (error & PF_RSVD) return VMM_EFAULT; // Corrupt page tables, nothing sensible to do for the user

	bool write = error & PF_WRITE;
	bool exec = error & PF_INSTR;

	vmm_lock_acquire(&as->lock);

	vma* v = vmm_vma_find(as, addr);
	if (!v) {
		// Maybe it's the stack growing, look for the first VMA above addr
		vma* above = as->vma_list;
		while (above && above->start <= addr) {
			above = above->next;
		}
		if (above && (above->flags & VMM_MAP_GROWSDOWN) && vmm_stack_grow(as, above, addr) == VMM_OK) {
			v = above;
			VMM_STAT(faults_stack_grow);
		}
	}

	int rc = VMM_EFAULT;
	if (v) {
		bool allowed;
		if (write) allowed = v->prot & VMM_PROT_WRITE;
		else if (exec) allowed = v->prot & VMM_PROT_EXEC;
		else allowed = v->prot != VMM_PROT_NONE; // Write and exec imply read on x86

		if (allowed) rc = vmm_fault_in_locked(as, v, addr, write);
	}

	vmm_lock_release(&as->lock);
	return rc;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Actual handler
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static void describe_fault(uint64_t addr, uint64_t err, struct interrupt_regs* regs) {
	printf_serial("\r\n[VMM] KERNEL PAGE FAULT\r\n");
	printf_serial("\taddress: 0x%llx\r\n", addr);
	// average printf_serial debug call
	printf_serial(
		"\terror:   0x%llx (%s, %s, %s%s%s%s%s)\r\n",
		err,
		(err & PF_PRESENT) ? "protection violation" : "page not present",
		(err & PF_WRITE) ? "write" : "read",
		(err & PF_USER) ? "user mode" : "kernel mode",
		(err & PF_RSVD) ? ", reserved bit set" : "",
		(err & PF_INSTR) ? ", instruction fetch" : "",
		(err & PF_PK) ? ", protection key" : "",
		(err & PF_SS) ? ", shadow stack" : ""
	);
	printf_serial("\trip:     0x%llx\r\n  rsp:     0x%llx\r\n  cs:      0x%llx\r\n", (uint64_t) regs->rip, (uint64_t) regs->rsp, (uint64_t) regs->cs);

	if (addr < PAGE_4KB_SIZE * 16) {
		printf_serial("\tLikely a NULL pointer dereference\r\n");
	} else if (addr >= KVA_ARENA_BASE && addr < KVA_ARENA_END) {
		printf_serial("\tKernel Arena. Likely a kernel stack guard page (stack overflow), or use after unmap/free of a mapping\r\n");
	} else if (vmm_is_direct_map_addr(addr)) {
		printf_serial("\tdirect map address?????\r\n");
	} else if (vmm_is_user_addr(addr)) {
		printf_serial("\tUser address from kernel mode. Missing copy_from_user()/copy_to_user(), or SMAP\r\n");
	}
}

WALLOS_INTERRUPT_HANDLER void page_fault_handler(struct interrupt_regs* regs) {
	uint64_t addr = vmm_read_cr2();
	uint64_t err = regs->error_code;
	bool from_user = err & PF_USER;
	VMM_STAT(faults_total);

	if (vmm_is_user_addr(addr)) {
		address_space_t* as = vmm_current_as();

		if (!from_user && (err & PF_PRESENT)) {
			// The kernel fetched an instruction from a user page that IS mapped. That's SMEP, never legitimate
			if (err & PF_INSTR) {
				describe_fault(addr, err, regs);
				panic_s("Kernel tried to execute user memory (SMEP violation).");
			}

			// The kernel touched a user page that IS mapped, with AC clear, and it wasn't a write to a COW page. That's SMAP.
			// Retrying would fault forever, so say what happened
			if (vmm_smap_on && !(regs->rflags & RFLAGS_AC)) {
				int lvl;
				uint64_t* e = vmm_user_walk(as, addr, &lvl);
				bool cow_write = (err & PF_WRITE) && e && (*e & BIT_PRESENT) && (*e & BIT_COW);
				if (!cow_write) {
					describe_fault(addr, err, regs);
					panic_s("Kernel accessed user memory outside copy_to_user/copy_from_user (SMAP violation).");
				}
			}
		}

		int rc = vmm_handle_user_fault(as, addr, err);
		if (rc == VMM_OK) {
			vmm_invlpg(addr);
			return;
		}

		if (from_user) {
			VMM_STAT(faults_user_fatal);
			if (user_fault_cb) {
				user_fault_cb(as, addr, err, regs); // this will never return
			}
			describe_fault(addr, err, regs);
			panic_s("Unhandled user page fault and no user fault handler installed (or it returned).");
		}

		// Kernel mode, user address, couldn't be fixed
		// If it's one of our copy instructions that's an ordinary "bad user pointer"
		if (regs->rip == (uint64_t) vmm_uaccess_copy_insn) {
			regs->rip = (uint64_t) vmm_uaccess_copy_fixup;
			VMM_STAT(faults_uaccess_fixup);
			return;
		}
		if (regs->rip == (uint64_t) vmm_uaccess_clear_insn) {
			regs->rip = (uint64_t) vmm_uaccess_clear_fixup;
			VMM_STAT(faults_uaccess_fixup);
			return;
		}

		describe_fault(addr, err, regs);
		panic_s("Kernel page fault on a user address.");
	}

	// Everything else
	// NULL page, the unmapped middle of the address space, the kernel half
	if (from_user) {
		address_space_t* as = vmm_current_as();
		VMM_STAT(faults_user_fatal);
		if (user_fault_cb) {
			user_fault_cb(as, addr, err, regs); // this will never return
		}
	}
	describe_fault(addr, err, regs);
	panic_s(from_user ? "Unhandled user page fault and no user fault handler installed (or it returned)." : "Kernel page fault.");
}
