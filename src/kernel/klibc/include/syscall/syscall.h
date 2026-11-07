#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <stddef.h>
#include <stdint.h>
#include <syscall/syscall_nums.h>
#include <syscall/syscall_returns.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYSCALL_COMMON_BASE 0
#define SYSCALL_COMMON_END  512  // [0, 512)     common
#define SYSCALL_ARCH_BASE   512
#define SYSCALL_ARCH_END    1024 // [512, 1024)  arch-specific
#define SYSCALL_MODULE_BASE 1024
#define SYSCALL_MODULE_END  2048 // [1024, 2048) modules / dynamic
#define SYSCALL_RSVD_BASE   2048 // >= 2048      reserved

	 /**
	  * @brief It is the architecture's syscall handler's responsibility to fill this struct in.
	  * The handler does not necessarily need to worry about how many arguments each call takes, calls will ignore fields from this that aren't used.
	  * Data results are written through user pointers passed in arg0..arg5 (validate them first).
	  * arguments_t::ret holds the E_* status after dispatch. The architecture code maps it to the user return register.
	  * The architecture's syscall handler should restore these to the same registers they were taken from.
	  */
	typedef struct {
		uint64_t syscall_num;
		uint64_t arg0;
		uint64_t arg1;
		uint64_t arg2;
		uint64_t arg3;
		uint64_t arg4;
		uint64_t arg5;
		uint64_t ret;
	} arguments_t;

	/** Handlers return an E_* status. */
	typedef int (*syscall_handler_t)(arguments_t*);

	typedef struct {
		syscall_handler_t handler; // NULL = unused slot
		const char* name;          // for logging
		uint8_t arg_count;         // for logging only, the dispatcher can't verify it
	} syscall_entry_t;

	/** A numbered entry, for registering tables of syscalls in one call. */
	typedef struct {
		uint64_t num;
		syscall_entry_t entry;
	} syscall_def_t;

// Macro for adding syscalls to a syscall table.
// See syscall_common.h
#define SYSCALL_DEF(num, name, argc) \
	{ \
		(num), { sys_##name, #name, (argc) } \
	}
#define SYSCALL(name) int sys_##name(arguments_t* args)

	/**
	 * @brief Register the entry at the provided number.
	 * The entry is copied.
	 *
	 * @retval E_INVALID if the number is out of range
	 * @retval E_EXIST if the number already exits
	 * @retval E_NOERROR if successful
	 */
	int syscall_register(uint64_t num, const syscall_entry_t* entry);

	/**
	 * @brief Register a dynamic system call (2048+ range).
	 * This is meant to be used by modules/drivers in the future.
	 * They must provide a mechanism for user applications to get the relevant syscall number (maybe a common syscall entry eventually).
	 *
	 * @param entry Syscall entry being added.
	 * @param out_num Holds the syscall number allocated to the module.
	 * @retval E_INVALID if out of range, or entry/out_num are NULL
	 * @retval E_NOSPACE if there is no space for new dynamic syscalls
	 * @retval E_NOERROR if successful
	 */
	int syscall_register_dynamic(const syscall_entry_t* entry, uint64_t* out_num);

	/**
	 * @brief Register `count` defs, stopping at the first failure and returning its status
	 * Entries before the failure stay registered.
	 *
	 * @param defs Array of syscall definitions to register.
	 * @param count Amount of syscalls in the array.
	 *
	 * @retval E_INVALID if the number is out of range or defs is undefined.
	 * @retval E_EXIST if the number already exits
	 * @retval E_NOERROR if successful
	 */
	int syscall_register_table(const syscall_def_t* defs, size_t count);

	/**
	 * @brief Remove the syscall at the provided number.
	 *
	 * @param num Syscall number to remove
	 *
	 * @retval E_INVALID bad number
	 * @retval E_NOENTRY empty slot
	 * @retval E_NOERROR if successful
	 */
	int syscall_unregister(uint64_t num);

	/**
	 * @brief Register all syscalls.
	 */
	void syscall_init();

	/**
	 * @brief Called by the architecture's syscall trap once `arguments_t` is filled in.
	 * Writes the handler's status (or E_NOSYS) into args->ret.
	 */
	void syscall_dispatch(arguments_t* args);

	/* Architecture specific functions */

	/**
	 * @note IMPORTANT: THIS IS NOT TO SET UP THE SYSCALL TRAP/HANDLER. THAT IS HANDLED BY CPU/SCHEDULER INIT.
	 *
	 * @brief Architecture provided function that registers all architecture specific syscalls.
	 * It is weakly defined, so an architecture does not have to explicitly define it if it has no syscalls.
	 */
	void arch_syscall_init();

#ifdef __cplusplus
}
#endif
#endif // KERNEL_SYSCALL_H