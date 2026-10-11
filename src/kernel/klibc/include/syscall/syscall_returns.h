#ifndef WALLOS_SYSCALL_RETURNS_H
#define WALLOS_SYSCALL_RETURNS_H

// This header will be exported as a user mode header, so syscall errors can be seen in user mode.

// These values are part of the ABI.
// Never renumber, only add.
// Values may mean different things to different calls where noted, but the meaning listed here is the default
// THESE SHOULD ALL BE POSITIVE.
// We may make a compat layer for linux/posix returns eventually, and those are all negative

enum {
	E_NOERROR = 0,   // Success.
	E_INVALID = 1,   // An argument was invalid. Bad pointer, out-of-range, NULL handler, or a syscall number outside the range the operation allows.
	E_EXIST = 2,     // The thing being created already exists (like syscall number already registered).
	E_NOSYSCALL = 3, // No syscall is registered at this number (including the reserved range).
	E_NOSPACE = 4,   // A fixed-size resource is full (like no free dynamic syscall slot).
	E_NOENTRY = 5,   // The thing being looked up or removed does not exist (like unregistering an empty slot).
	E_BUSY = 6,      // The resource is in use and can't be changed right now.
};

#endif // WALLOS_SYSCALL_RETURNS_H