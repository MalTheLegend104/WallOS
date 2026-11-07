# Syscalls

Due to the importance of syscalls, this documentation is in tree, rather than with the rest of the docs.

## Syscall Numbers

| Range     | Owner                                                  |
| --------- | ------------------------------------------------------ |
| 0-511     | Common, defined in `syscall_nums.h`                    |
| 512-1023  | Architecture-specific, `<arch>/syscall/syscall_nums.h` |
| 1024-2047 | Modules / dynamically registered                       |
| 2048+     | Reserved. Always returns `E_NOSYSCALL`                 |

Some rules:

- Numbers are part of the ABI.
- **Never renumber or reuse one.**
  - If one must be removed, retire it by leaving a hole and a comment explaining why it was removed.
- Always set the number explicitly in the table, don't rely on the enum being filled by the compiler.
- Registering a number outside the caller's range, or one that is already taken, fails (`E_INVALID` / `E_EXIST`), and a bad common table panics at boot.

## Adding a Syscall

All common syscalls are declared and tabled in `syscall_common.h`.

1. Add an explicit number to `syscall_nums.h`
2. Declare the handler in `syscall_common.h`:
    - `SYSCALL(foo);` (this expands to `int sys_foo(arguments_t* args)`. Don't write the `sys_` prefix yourself).
3. Add a row to `common_syscalls[]` in the same file:
    - `SYSCALL_DEF(SYS_FOO, foo, arg_count)`.
      - `arg_count` is used for logging only, the dispatcher can't verify it
4. Implement it in a .c file: `SYSCALL(foo) { ... return E_NOERROR; }`
   - They can be implemented in C++, make sure it has C linkage.

Keep prototypes and rows in number order.

Handlers return an `E_*` status (see `syscall_returns.h`). The dispatcher stores it in `args->ret`.
Data results go out through user pointers in `arg0..arg5`, which the handler must validate first.

## Architecture Requirements

- Fill `arguments_t` from the trap frame, then call `syscall_dispatch`.
- Copy `args.ret` to the user return register on exit.
- Register arch syscalls (512-1023) from `arch_syscall_init` by building a `syscall_def_t` table
  with `SYSCALL_DEF` rows and calling `syscall_register_table`.
- Provide `<arch>/syscall/syscall_nums.h` for the exported arch numbers.

## x86_64

The syscall handler itself is handled as a special case in the scheduler code.

```c
int64_t x86_syscall(x86_regs_t* r);
```

This translates the registers pushed into the context by the scheduler handler, translates them (using the ABI below), calls dispatch, translates them back, then returns.

| Hardware Register | Syscall `arguments_t` |
| ----------------- | --------------------- |
| RAX (entry)       | syscall_num           |
| RDI               | arg0                  |
| RSI               | arg1                  |
| RDX               | arg2                  |
| R10               | arg3                  |
| R8                | arg4                  |
| R9                | arg5                  |
| RAX (return)      | ret                   |

> This is the same as linux, and SysV conventions, replacing rcx with r11 as the hardware clobbers it.
> r11 is also clobbered by `syscall`
