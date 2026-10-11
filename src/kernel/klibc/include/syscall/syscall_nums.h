#ifndef WALLOS_SYSCALL_NUMS_H
#define WALLOS_SYSCALL_NUMS_H

// This header will be exported as a user mode header, so syscall numbers can be seen in user mode.
// Always explicitly set the syscall number.

// These values are part of the ABI.
// Never renumber, only add.
// If one needs to be removed, leave that number empty
enum {
	SYS_EXIT = 0,
	SYS_YIELD = 1,

};

#endif // WALLOS_SYSCALL_NUMS_H