#ifndef WALLOS_SYSCALL_RETURNS_H
#define WALLOS_SYSCALL_RETURNS_H

// This header will be exported as a user mode header, so syscall errors can be seen in user mode.

// These values are part of the ABI.
// Never renumber, only add.
// Values may mean different things to different calls where noted, but the meaning listed here is the default
// THESE SHOULD ALL BE POSITIVE.
// We may make a compat layer for linux/posix returns eventually, and those are all negative
enum {
	E_NOERROR = 0,      // Success.
	E_INVALID = 1,      // An argument was invalid. Bad pointer, out-of-range, NULL handler, or a syscall number outside the range the operation allows.
	E_EXIST = 2,        // The thing being created already exists (like syscall number already registered).
	E_NOSYSCALL = 3,    // No syscall is registered at this number (including the reserved range).
	E_NOSPACE = 4,      // A fixed-size resource is full (syscall slot, disk space, etc.).
	E_NOENTRY = 5,      // The thing being looked up or removed does not exist (like unregistering an empty slot).
	E_BUSY = 6,         // The resource is in use and can't be changed right now.
	E_BADF = 7,         // Bad file descriptor (like trying to write to a read-only fd).
	E_IO = 8,           // Device or backing-store failure.
	E_FAULT = 9,        // Bad user pointer.
	E_NOMEM = 10,       // Kernel allocation failed.
	E_ACCESS = 11,      // Permission denied.
	E_NOTDIR = 12,      // FD points to a file not a directory
	E_ISDIR = 13,       // FD points to a directory not a file
	E_SPIPE = 14,       // Not seekable. fd is associated with a pipe, socket, or FIFO.
	E_NOTTY = 15,       // The fd is not a terminal (like a TTY ioctl or isatty check on a regular file).
	E_INTR = 16,        // The call was interrupted before it completed (like a signal arriving during a blocking wait). The caller may retry.
	E_AGAIN = 17,       // The operation can't complete right now, but may later (like a non-blocking read with no data, or a futex_wait whose value no longer matches).
	E_TIMEDOUT = 18,    // The timeout expired before the operation completed (like futex_wait or a timed wait).
	E_MFILE = 19,       // Per-process fd table is full.
	E_NOTSUP = 20,      // The operation is valid but not supported by this object or kernel (like an ioctl or flag a device doesn't implement).
	E_NAMETOOLONG = 21, // A path (or path component) exceeds the maximum length.
	E_NOTEMPTY = 22,    // The directory is not empty.
	E_OVERFLOW = 23,    // A value (like a file offset) does not fit in the type or exceeds file bounds.
	E_NFILE = 24,       // The system-wide open file table is full (as opposed to E_MFILE, the per-process table).
};


#endif // WALLOS_SYSCALL_RETURNS_H