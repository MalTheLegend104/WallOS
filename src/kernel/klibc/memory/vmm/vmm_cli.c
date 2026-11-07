#include <drivers/serial.h>
#include <memory/virtual_mem.h>
#include <memory/vmm_internal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <terminal/terminal.h>

const ws_command_argument_t virt_mem_cli_args[] = {
	{WS_ARG_TYPE_GENERIC, false, "command", NULL, "One of: help, info, features, stats, walk, v2p, dump, as, vma, arena, direct."},
	{WS_ARG_TYPE_GENERIC, false, "argument", NULL, "Virtual address (walk/v2p), table name or virtual address of a table (dump: pml4 kpml4 low direct arena kimage), or an address space id (vma)."},
};
const size_t virt_mem_cli_args_count = sizeof(virt_mem_cli_args) / sizeof(virt_mem_cli_args[0]);

static void print_entry_flags(uint64_t entry) {
	printf_serial("  flags: %s%s%s%s%s%s%s%s%s%s%s\r\n",
				  (entry & BIT_NX) ? "NX " : "",
				  (entry & BIT_GLOBAL) ? "GLOBAL " : "",
				  (entry & BIT_SIZE) ? "PS/PAT " : "",
				  (entry & BIT_DIRTY) ? "DIRTY " : "",
				  (entry & BIT_ACCESS) ? "ACCESS " : "",
				  (entry & BIT_PCD) ? "PCD " : "",
				  (entry & BIT_PWT) ? "PWT " : "",
				  (entry & BIT_USR) ? "USR " : "",
				  (entry & BIT_WRITE) ? "RW " : "",
				  (entry & BIT_COW) ? "COW " : "",
				  (entry & BIT_PRESENT) ? "PRESENT" : "NOT-PRESENT");
}

static void print_level(const char* label, int idx, uint64_t entry) {
	bool present = entry & 1;
	printf_color(present ? PRINT_COLOR_LIGHT_GREEN : PRINT_COLOR_LIGHT_RED,
				 PRINT_DEFAULT_BG,
				 "  %s[%d] = 0x%llx%s\n",
				 label,
				 idx,
				 (uint64_t) entry,
				 present ? "" : "  !! NOT PRESENT");
	printf_serial("  %s[%d] = 0x%llx\r\n", label, idx, (uint64_t) entry);
	print_entry_flags(entry);
}

static void cmd_walk(uint64_t vaddr) {
	/* walk + dump a single virtual address, in the address space this CPU is running */
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "\n[VMM WALK] vaddr = 0x%llx\n", vaddr);
	printf_serial("\r\n[VMM WALK] vaddr = 0x%llx\r\n", vaddr);

	int pml4_i = GET_PML4_INDEX(vaddr);
	int pdp_i = GET_PDPT_INDEX(vaddr);
	int pde_i = GET_PAGE_DIR_INDEX(vaddr);
	int pte_i = GET_PAGE_TABLE_INDEX(vaddr);
	int off = vaddr & 0xFFF;

	printf_color(PRINT_DEFAULT_FG, PRINT_DEFAULT_BG, "  indices: pml4[%d] pdp[%d] pde[%d] pte[%d] offset=0x%x\n", pml4_i, pdp_i, pde_i, pte_i, off);
	printf_serial("  indices: pml4[%d] pdp[%d] pde[%d] pte[%d] offset=0x%x\r\n", pml4_i, pdp_i, pde_i, pte_i, off);

	static const char* names[5] = {"", "PTE ", "PDE ", "PDP ", "PML4"};
	uint64_t* table = vmm_current_root();

	for (int lvl = 4; lvl >= 1; lvl--) {
		int idx = VMM_LVL_INDEX(vaddr, lvl);
		uint64_t e = table[idx];
		print_level(names[lvl], idx, e);

		if (!(e & 1)) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "  Walk terminated at %s.\n", names[lvl]);
			return;
		}

		if (lvl == 1 || (lvl < 4 && (e & BIT_SIZE))) {
			uint64_t size = VMM_LVL_SIZE(lvl);
			uint64_t phys = vmm_entry_frame(e, lvl) + (vaddr & (size - 1));
			const char* label = lvl == 3 ? "1GB" : (lvl == 2 ? "2MB" : "4KB");
			printf_color(lvl == 1 ? PRINT_COLOR_LIGHT_GREEN : PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "  %s PAGE  ->  phys = 0x%llx\n", label, phys);
			printf_serial("  %s PAGE  ->  phys = 0x%llx\r\n", label, phys);
			return;
		}
		table = vmm_table_ptr(e & PTE_ADDR_4K);
	}
}

/* Dump all present entries in a single table
 * Bulk entry data goes to serial only; terminal just gets a header and a summary line so it doesn't get flooded.
 */
static void dump_table(const char* name, uint64_t* table, size_t entries) {
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "\n[VMM DUMP] %s\n", name);
	printf_color(PRINT_COLOR_DARK_GREY, PRINT_DEFAULT_BG, "  (full entry listing on serial)\n");

	printf_serial("\r\n[VMM DUMP] %s (%zu entries, base @ %p)\r\n", name, entries, (void*) table);

	int present_count = 0;
	for (size_t i = 0; i < entries; i++) {
		if (!(table[i] & 1)) {
			continue;
		}
		present_count++;
		printf_serial(
			"  [%03zu] 0x%llx  frame=0x%llx%s%s%s%s%s\r\n",
			i,
			table[i],
			(uint64_t) (table[i] & PTE_ADDR_4K),
			(table[i] & BIT_SIZE) ? " PS" : "",
			(table[i] & BIT_USR) ? " USR" : "",
			(table[i] & BIT_WRITE) ? " RW" : "",
			(table[i] & BIT_COW) ? " COW" : "",
			(table[i] & BIT_NX) ? " NX" : ""
		);
	}

	/* green if anything mapped, yellow if nothing */
	int summary_fg = (present_count > 0) ? PRINT_COLOR_LIGHT_GREEN : PRINT_COLOR_YELLOW;
	printf_color(summary_fg, PRINT_DEFAULT_BG, "  %d / %zu entries present\n", present_count, entries);
	printf_serial("  %d / %zu entries present\r\n", present_count, entries);
}

/* translate virtual address to physical */
static void cmd_v2p(uint64_t vaddr) {
	uintptr_t phys = vmm_virt_to_phys_base(vaddr);
	uintptr_t full = virt_to_phys(vaddr);
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "\n[VMM V2P]\n");
	printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "  virt       ");
	printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG, "0x%llx\n", vaddr);
	printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "  page base  ");
	printf_color(phys ? PRINT_COLOR_LIGHT_GREEN : PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "0x%llx%s\n", (uint64_t) phys, phys ? "" : "  (not mapped)");
	printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "  phys       ");
	printf_color(phys ? PRINT_COLOR_LIGHT_GREEN : PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "0x%llx\n", (uint64_t) full);
	printf_serial("[VMM V2P] virt 0x%llx  ->  base 0x%llx  phys 0x%llx\r\n", vaddr, (uint64_t) phys, (uint64_t) full);
}

/* One row: label in light-grey, value in white */
// I really should make a standard, I copied this from the old one and I like how it looks, but it should be consistent across the OS
#define INFO_ROW(label, fmt, ...) \
	do { \
		printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "  %-22s", label); \
		printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG, fmt "\n", ##__VA_ARGS__); \
		printf_serial("  %-22s" fmt "\r\n", label, ##__VA_ARGS__); \
	} while (0)

static void cmd_info(void) {
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "\n[VMM INFO]\n");
	printf_serial("\r\n[VMM INFO]\r\n");

	size_t pool_used, pool_total;
	vmm_early_pool_stats(&pool_used, &pool_total);

	INFO_ROW("KERNEL_VIRTUAL_BASE:", "0x%llx", (uint64_t) KERNEL_VIRTUAL_BASE);
	INFO_ROW("kernel_mapping_end:", "0x%llx", (uint64_t) vmm_get_mapping_end());
	INFO_ROW("DIRECT_MAP_BASE:", "0x%llx", (uint64_t) DIRECT_MAP_BASE);
	INFO_ROW("KVA arena:", "0x%llx - 0x%llx", (uint64_t) KVA_ARENA_BASE, (uint64_t) KVA_ARENA_END);
	INFO_ROW("user range:", "0x%llx - 0x%llx", (uint64_t) USER_VIRTUAL_BASE, (uint64_t) USER_VIRTUAL_END);
	INFO_ROW("kernel pml4:", "virt %p  phys 0x%llx", (void*) vmm_kernel_pml4, (uint64_t) vmm_kernel_pml4_phys);
	INFO_ROW("current pml4:", "virt %p  phys 0x%llx", (void*) vmm_current_as()->pml4, (uint64_t) vmm_current_as()->pml4_phys);
	INFO_ROW("PMM ready:", "%s", vmm_pmm_ready ? "yes (tables come from the buddy allocator)" : "no (early pool)");
	INFO_ROW("early pool used:", "%zu / %zu pages", pool_used, pool_total);

	int mfg = vmm_pmm_ready ? PRINT_COLOR_LIGHT_GREEN : PRINT_COLOR_YELLOW;
	(void) mfg;

	for (int slot = 0; slot < TABLE_ENTRIES; slot++) {
		if (!(vmm_kernel_pml4[slot] & 1)) {
			continue;
		}
		INFO_ROW("kernel pml4 slot:", "[%d] -> 0x%llx", slot, (uint64_t) (vmm_kernel_pml4[slot] & PTE_ADDR_4K));
	}
}

static void cmd_features(void) {
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "\n[VMM FEATURES]\n");
	printf_serial("\r\n[VMM FEATURES]\r\n");

	INFO_ROW("NX:", "%s", vmm_feat.nx ? "yes" : "NO");
	INFO_ROW("PAT:", "%s", vmm_feat.pat ? "yes" : "NO");
	INFO_ROW("global pages:", "%s", vmm_feat.pge ? "yes" : "NO");
	INFO_ROW("PCID / INVPCID:", "%s / %s  (using PCID: %s)", vmm_feat.pcid ? "yes" : "no", vmm_feat.invpcid ? "yes" : "no", vmm_use_pcid ? "yes" : "no");
	INFO_ROW("SMEP:", "%s", vmm_feat.smep ? "yes" : "no");
	INFO_ROW("SMAP:", "%s%s", vmm_feat.smap ? "yes" : "no", vmm_smap_on ? "  (enforced)" : "");
	INFO_ROW("1GB pages:", "%s", vmm_feat.gb_pages ? "yes" : "no");
	if (vmm_feat.pat) {
		INFO_ROW("IA32_PAT (this CPU):", "0x%llx", (uint64_t) vmm_rdmsr(0x277));
	}
	INFO_ROW("CR0 / CR4:", "0x%llx / 0x%llx", (uint64_t) vmm_read_cr0(), (uint64_t) vmm_read_cr4());
}

static void cmd_stats(void) {
	const vmm_stats* s = vmm_get_stats();
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "\n[VMM STATS]\n");
	printf_serial("\r\n[VMM STATS]\r\n");

	INFO_ROW("page faults:", "%llu", (unsigned long long) s->faults_total);
	INFO_ROW("  demand paged:", "%llu", (unsigned long long) s->faults_demand);
	INFO_ROW("  copy on write:", "%llu", (unsigned long long) s->faults_cow);
	INFO_ROW("  spurious:", "%llu", (unsigned long long) s->faults_spurious);
	INFO_ROW("  stack growth:", "%llu", (unsigned long long) s->faults_stack_grow);
	INFO_ROW("  uaccess fixups:", "%llu", (unsigned long long) s->faults_uaccess_fixup);
	INFO_ROW("  fatal (user):", "%llu", (unsigned long long) s->faults_user_fatal);
	INFO_ROW("local TLB flushes:", "%llu", (unsigned long long) s->tlb_local_flushes);
	INFO_ROW("shootdowns:", "%llu  (%llu IPIs)", (unsigned long long) s->tlb_shootdowns, (unsigned long long) s->tlb_ipis_sent);
	INFO_ROW("address spaces:", "%llu created, %llu destroyed", (unsigned long long) s->as_created, (unsigned long long) s->as_destroyed);
}

static void emit_as(address_space_t* as) {
	size_t vmas = 0;
	for (vma* v = as->vma_list; v; v = v->next) {
		vmas++;
	}
	printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "  #%-3u", as->id);
	printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG, "cr3 0x%llx  pcid %u  refs %u  vmas %zu  cpus 0x%llx%s%s\n", (uint64_t) as->pml4_phys, (unsigned) as->pcid, (unsigned) as->refcount, vmas, (uint64_t) as->cpu_mask[0], as->is_kernel ? "  (kernel)" : "", as == vmm_current_as() ? "  <- current" : "");
	printf_serial(
		"  #%-3u cr3 0x%llx  pcid %u  refs %u  vmas %zu  cpus 0x%llx%s\r\n",
		as->id,
		(uint64_t) as->pml4_phys,
		(unsigned) as->pcid,
		(unsigned) as->refcount,
		vmas,
		(uint64_t) as->cpu_mask[0],
		as->is_kernel ? "  (kernel)" : ""
	);
}

static void cmd_as(void) {
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "\n[VMM ADDRESS SPACES]\n");
	printf_serial("\r\n[VMM ADDRESS SPACES]\r\n");
	vmm_as_list(emit_as);
}

static uint32_t vma_target_id;
static address_space_t* vma_target = NULL;

static void find_as(address_space_t* as) {
	if (as->id == vma_target_id) {
		vma_target = as;
	}
}

static void cmd_vma(bool have_id, uint32_t id) {
	address_space_t* as = vmm_current_as();
	if (have_id) {
		vma_target_id = id;
		vma_target = NULL;
		vmm_as_list(find_as);
		if (!vma_target) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[VMM] no address space #%u (try 'vmm as')\n", id);
			return;
		}
		as = vma_target;
	}

	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "\n[VMM VMAS] address space #%u\n", as->id);
	printf_serial("\r\n[VMM VMAS] address space #%u\r\n", as->id);

	vmm_lock_acquire(&as->lock);
	int count = 0;
	for (vma* v = as->vma_list; v; v = v->next) {
		count++;
		char prot[4] = {(v->prot & VMM_PROT_READ) ? 'r' : '-', (v->prot & VMM_PROT_WRITE) ? 'w' : '-', (v->prot & VMM_PROT_EXEC) ? 'x' : '-', 0};
		printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG, "  0x%llx-0x%llx %s %s%s%s%s\n", (uint64_t) v->start, (uint64_t) v->end, prot, (v->flags & VMM_MAP_SHARED) ? "shared " : "private ", (v->flags & VMM_MAP_HUGE) ? "huge " : "", (v->flags & VMM_MAP_GROWSDOWN) ? "stack " : "", v->ops ? "backed" : "anon");
		printf_serial(
			"  0x%llx-0x%llx %s %s%s%s%s  (%llu KB)\r\n",
			(uint64_t) v->start,
			(uint64_t) v->end,
			prot,
			(v->flags & VMM_MAP_SHARED) ? "shared " : "private ",
			(v->flags & VMM_MAP_HUGE) ? "huge " : "",
			(v->flags & VMM_MAP_GROWSDOWN) ? "stack " : "",
			v->ops ? "backed" : "anon",
			(unsigned long long) ((v->end - v->start) >> 10)
		);
	}
	vmm_lock_release(&as->lock);

	printf_color(count ? PRINT_COLOR_LIGHT_GREEN : PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "  %d VMAs\n", count);
}

static uint64_t arena_total_free;

static void emit_arena(uintptr_t start, size_t size) {
	arena_total_free += size;
	printf_serial("  0x%llx - 0x%llx  (%llu KB)\r\n", (uint64_t) start, (uint64_t) (start + size), (unsigned long long) (size >> 10));
}

static void cmd_arena(void) {
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "\n[VMM ARENA] free ranges (listing on serial)\n");
	printf_serial("\r\n[VMM ARENA] free ranges\r\n");
	arena_total_free = 0;
	vmm_kva_dump(emit_arena);
	uint64_t total = KVA_ARENA_END - KVA_ARENA_BASE;
	printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "  %llu MB free of %llu MB\n", (unsigned long long) (arena_total_free >> 20), (unsigned long long) (total >> 20));
	printf_serial("  %llu MB free of %llu MB\r\n", (unsigned long long) (arena_total_free >> 20), (unsigned long long) (total >> 20));
}

static uint64_t direct_total;

static void emit_direct(uint64_t start, uint64_t end) {
	direct_total += end - start;
	printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG, "  phys 0x%llx - 0x%llx  (%llu MB)\n", start, end, (unsigned long long) ((end - start) >> 20));
	printf_serial("  phys 0x%llx - 0x%llx  -> virt 0x%llx\r\n", start, end, (uint64_t) (DIRECT_MAP_BASE + start));
}

static void cmd_direct(void) {
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "\n[VMM DIRECT MAP] base 0x%llx\n", (uint64_t) DIRECT_MAP_BASE);
	printf_serial("\r\n[VMM DIRECT MAP] base 0x%llx\r\n", (uint64_t) DIRECT_MAP_BASE);
	direct_total = 0;
	vmm_direct_dump(emit_direct);
	printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "  %llu MB mapped\n", (unsigned long long) (direct_total >> 20));
}

// A kernel slot's PDPT, for 'dump'
static uint64_t* slot_table(int slot) {
	if (!(vmm_kernel_pml4[slot] & 1)) {
		return NULL;
	}
	return vmm_table_ptr(vmm_kernel_pml4[slot] & PTE_ADDR_4K);
}

int virt_mem_cli(int argc, char** argv) {
	ws_context_t* ctx = ws_getCurrentContext();

	if (!ws_parse_args(ctx, argc, argv) || !ws_has_arg(ctx, "command")) {
		printf("Run `vmm -h` for help.");
		return 0;
	}

	const char* cmd = ws_get_generic(ctx, "command");

	if (strcmp(cmd, "info") == 0) {
		cmd_info();

	} else if (strcmp(cmd, "features") == 0) {
		cmd_features();

	} else if (strcmp(cmd, "stats") == 0) {
		cmd_stats();

	} else if (strcmp(cmd, "as") == 0) {
		cmd_as();

	} else if (strcmp(cmd, "vma") == 0) {
		if (ws_has_arg(ctx, "argument")) {
			cmd_vma(true, (uint32_t) strtoull(ws_get_generic(ctx, "argument"), NULL, 0));
		} else {
			cmd_vma(false, 0);
		}

	} else if (strcmp(cmd, "arena") == 0) {
		cmd_arena();

	} else if (strcmp(cmd, "direct") == 0) {
		cmd_direct();

	} else if (strcmp(cmd, "walk") == 0) {
		if (!ws_has_arg(ctx, "argument")) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[VMM] usage: walk <vaddr>\n");
			return 1;
		}
		cmd_walk((uint64_t) strtoull(ws_get_generic(ctx, "argument"), NULL, 0));

	} else if (strcmp(cmd, "v2p") == 0) {
		if (!ws_has_arg(ctx, "argument")) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[VMM] usage: v2p <vaddr>\n");
			return 1;
		}
		cmd_v2p((uint64_t) strtoull(ws_get_generic(ctx, "argument"), NULL, 0));

	} else if (strcmp(cmd, "dump") == 0) {
		if (!ws_has_arg(ctx, "argument")) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[VMM] usage: dump <table>\n");
			return 1;
		}
		const char* tbl = ws_get_generic(ctx, "argument");
		uint64_t* table = NULL;
		const char* name = tbl;

		if (strcmp(tbl, "pml4") == 0) {
			table = vmm_current_as()->pml4;
		} else if (strcmp(tbl, "kpml4") == 0) {
			table = vmm_kernel_pml4;
		} else if (strcmp(tbl, "low") == 0) {
			table = slot_table(0);
		} else if (strcmp(tbl, "direct") == 0) {
			table = slot_table(GET_PML4_INDEX(DIRECT_MAP_BASE));
		} else if (strcmp(tbl, "arena") == 0) {
			table = slot_table(GET_PML4_INDEX(KVA_ARENA_BASE));
		} else if (strcmp(tbl, "kimage") == 0) {
			table = slot_table(511);
		} else if (tbl[0] == '0' && (tbl[1] == 'x' || tbl[1] == 'X')) {
			table = (uint64_t*) strtoull(tbl, NULL, 0);
			if ((uint64_t) table & (PAGE_4KB_SIZE - 1)) {
				table = NULL;
			}
		}

		if (!table) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[VMM] unknown or empty table '%s'\n       try: pml4 kpml4 low direct arena kimage, or the virtual address of a table\n", tbl);
			printf_serial("[VMM] unknown table '%s'\r\n", tbl);
			return 1;
		}
		dump_table(name, table, TABLE_ENTRIES);

	} else {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[VMM] unknown command '%s' — try 'help'\n", cmd);
		printf_serial("[VMM] unknown command '%s'\r\n", cmd);
		return 1;
	}

	return 0;
}

#undef INFO_ROW
