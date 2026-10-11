#include <drivers/serial.h>
#include <stdio.h>
#include <string.h>
#include <syscall/syscall.h>
#include <syscall/syscall_common.h>
#include <syscall/syscall_returns.h>
#include <wallos_attributes.h>

static syscall_entry_t common_table[SYSCALL_COMMON_END - SYSCALL_COMMON_BASE];
static syscall_entry_t arch_table[SYSCALL_ARCH_END - SYSCALL_ARCH_BASE];
static syscall_entry_t module_table[SYSCALL_MODULE_END - SYSCALL_MODULE_BASE];

WALLOS_WEAK void arch_syscall_init() {}

/* Returns the slot for `num`, or NULL if it's in the reserved range. */

/**
 * @brief Lookup the syscall entry for the given number
 *
 * @param num Number of the syscall to get
 * @return syscall_entry_t* for the slot, NULL if reserved
 */
static syscall_entry_t* lookup(uint64_t num) {
	if (num < SYSCALL_COMMON_END) return &common_table[num - SYSCALL_COMMON_BASE];
	if (num < SYSCALL_ARCH_END) return &arch_table[num - SYSCALL_ARCH_BASE];
	if (num < SYSCALL_MODULE_END) return &module_table[num - SYSCALL_MODULE_BASE];
	return NULL;
}

#if WALLOS_SYSCALL_LOG
static void log_syscall(const syscall_entry_t* e, const arguments_t* a) {
	const uint64_t argv[6] = {a->arg0, a->arg1, a->arg2, a->arg3, a->arg4, a->arg5};
	printf_serial("syscall %llu %s(", (unsigned long long) a->syscall_num, e->name);
	for (uint8_t i = 0; i < e->arg_count && i < 6; i++) {
		// this is a bit cursed. it will print the comma on things that aren't the last one
		printf_serial(i ? ", 0x%llx" : "0x%llx", (unsigned long long) argv[i]);
	}
	printf_serial(")\r\n");
}
#endif

int syscall_register(uint64_t num, const syscall_entry_t* entry) {
	syscall_entry_t* slot = lookup(num);
	if (!slot || !entry || !entry->handler) return E_INVALID;
	if (slot->handler) return E_EXIST;
	*slot = *entry; // we copy it, the caller struct can be temporary
	return E_NOERROR;
}

int syscall_register_dynamic(const syscall_entry_t* entry, uint64_t* out_num) {
	if (!entry || !entry->handler || !out_num) return E_INVALID;
	for (uint64_t n = SYSCALL_MODULE_BASE; n < SYSCALL_MODULE_END; n++) {
		if (!module_table[n - SYSCALL_MODULE_BASE].handler) {
			int r = syscall_register(n, entry);
			if (r == E_NOERROR) *out_num = n;
			return r;
		}
	}
	return E_NOSPACE;
}

int syscall_register_table(const syscall_def_t* defs, size_t count) {
	if (!defs) return E_INVALID;
	for (size_t i = 0; i < count; i++) {
		int r = syscall_register(defs[i].num, &defs[i].entry);
		if (r != E_NOERROR) return r;
	}
	return E_NOERROR;
}

int syscall_unregister(uint64_t num) {
	syscall_entry_t* slot = lookup(num);
	if (!slot) return E_INVALID;
	if (!slot->handler) return E_NOENTRY;
	// if we ever do module unloading, we would need to return E_BUSY here and track current calls
	memset(slot, 0, sizeof *slot);
	return E_NOERROR;
}

void syscall_dispatch(arguments_t* args) {
	syscall_entry_t* e = lookup(args->syscall_num);
	if (!e || !e->handler) {
		args->ret = E_NOSYSCALL;
		return;
	}
#if WALLOS_SYSCALL_LOG
	log_syscall(e, args);
#endif
	args->ret = (uint64_t) e->handler(args);
}

void syscall_init() {
	// register all of our "common" syscalls
	// yet again a bit cursed, just ignore it :)
	int r = syscall_register_table(common_syscalls, sizeof common_syscalls / sizeof *common_syscalls);
	if (r != E_NOERROR) {
		printf_color(PRINT_COLOR_RED, PRINT_DEFAULT_BG, "[SYSCALL][FATAL] bad common syscall table (%d)\n", r);
		WALLOS_CLI_HLT();
	}

	// any architecture specific ones
	arch_syscall_init();
	// any ones required by modules or otherwise will be registered when they are set up
}