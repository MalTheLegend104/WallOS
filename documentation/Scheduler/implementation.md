# Scheduler Implementation

This document describes how the WallOS "Fair Enough" Scheduler (WFES) is implemented.
It covers everything the [specification](./wfes_spec.tex) that is implementation defined (or otherwise intentionally left out of the spec).

- For _how the scheduler should behave_, read the specification.
- For _adding support for a new architecture_, read [porting.md](porting.md).

Section names in quotes refer to sections of the specification.

---

## 1. Source Layout

> All of these are either in `src/kernel/klibc/scheduler` (`.c`) or `src/kernel/klibc/include/scheduler` (`.h`).

| File                                                                 | Contents                                                                       |
| -------------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| `task.h`                                                             | Task control block (`task_t`), creation parameters, flags, constants           |
| `cpu.h`                                                              | Per-CPU state (`cpu_t`), runqueue list type, lock type                         |
| `arch.h`                                                             | The contract between the core and an architecture port (see PORTING.md)        |
| `scheduler.h`                                                        | Public API                                                                     |
| `sched_internal.h`                                                   | Locks, list helpers, load measure, prototypes shared between the `.c` files    |
| `scheduler.c`                                                        | Selection, the scheduling handler, idle task, bring-up                         |
| `balance.c`                                                          | Moving tasks, stealing, periodic balancing, idle mask, takeover redistribution |
| `task.c`                                                             | Creation, destruction, kill/suspend/resume, blocking, boot-task adoption       |
| `task_spawn.h`                                                       |                                                                                |
| `x86_64/arch_cpu.h`, `x86_64/sched_arch.c`, `x86_64/sched_stubs.asm` | The x86_64 port (`src/kernel/klibc`, `src/kernel/klibc/include`).              |

## 2. Architecture Overview

This is just a description of what is happening at a very high level:

- Each CPU owns one set of runqueues and manages it's own scheduling state.
- Tasks live on exactly one runqueue (or a very small subset of off-queue spots, mostly for currently running or prempted tasks).
- This scheduler is basically a _**very**_ fancy round-robin scheduler.
  - There are different priorities, which determine how much runtime a task gets.
  - Tasks can have affinity, "soft" or "hard".
    - Soft can be moved under certain circumstances, hard (normally) can't.
  - There are special priority levels that interleave with normal tasks.
  - There are special "kernel" tasks
    - "Kernel Task" in this context means a special task, not just something running in kernel mode. A "normal" task can be run in a kernel context, the scheduler doesn't care.

## 3. Scheduling Flow

Yet again, very high level overview of the general flow of the scheduler.

```
timer / IPI / yield
       │
       V
┌──────────────┐
│ sched_handle │
└──────┬───────┘
       │
       V
┌─────────────────┐
│ early-return?   │
└──────┬──────┬───┘
       │ yes  │ no
       V      V
return ctx  reap zombies
                │
                V
            save context
                │
                V
         dispose outgoing
                │
                V
      takeover redistribute
         (if applicable)
                │
                V
         periodic balance
             (if due)
                │
                V
           select task
                │
       ┌────────┴────────┐
       │                 │
    selected           empty
       │                 │
       │                 V
       │            try to steal
       │                 │
       │          ┌──────┴──────┐
       │          │             │
       │       success        failed
       │          │             │
       │          V             V
       │       reselect    idle fallback
       │          │             │
       └──────────┴─────────────┘
                  │
                  V
             arm timer
                  │
                  V
            return context
```

Most of this info is repeated below in more detail.

## 4. Scheduler Invariants

These are the important implementation invariants to keep in mind when modifying the scheduler:

- **Never hold two runqueue locks at once.**
- **Never touch a task after waking it.**
- **Cancel hooks run on the scheduling handler's stack, with interrupts disabled and no runqueue lock held.**
- **Do not free anything from the handler while it is running on that object's stack.**

> Most of these are also discussed below in their respective sections.

## 5. Tasks (TCB)

| Field                                          | Type                | Purpose                                                                                              |
| ---------------------------------------------- | ------------------- | ---------------------------------------------------------------------------------------------------- |
| `id`                                           | `uint64_t`          | Unique identifier; 0 (`TASK_ID_INVALID`) is invalid.                                                 |
| `saved_ctx`                                    | `void*`             | Opaque context handle owned by the port.                                                             |
| `stack_base`, `stack_size`, `owns_stack`       |                     | The task's stack. `owns_stack` is false for the adopted boot thread.                                 |
| `flags`                                        | `volatile uint32_t` | Atomic `TASK_F_*` bits.                                                                              |
| `cpu`                                          | `volatile uint32_t` | Owning CPU (logical id). Atomic; written only inside `rq_add_locked()`.                              |
| `priority`, `affinity`                         | `uint8_t`           | 0-9 (255 for kernel), and an `affinity_t`.                                                           |
| `is_kernel`, `kflags`, `kquanta`               |                     | Kernel task behavior (section 5.3).                                                                  |
| `remaining_us`                                 | `uint32_t`          | Quanta left when displaced by a kernel preemption; 0 means a full quantum.                           |
| `entry`, `arg`                                 |                     | Entry function and argument.                                                                         |
| `owns_arg`                                     | `bool`              | `arg` was `kalloc`'d for this task and is freed with it (`TASK_CREATE_OWN_ARG`).                     |
| `rq_next`, `rq_prev`                           |                     | Runqueue links. Also reused as the reap-chain and zombie-list link once the task is off every queue. |
| `parent`, `child_head`, `sib_next`, `sib_prev` |                     | Parent/child tree (protected by the tree lock).                                                      |
| `hash_next`                                    |                     | Id registry chain (tree lock).                                                                       |
| `blocker_cancel`, `blocker_ctx`                |                     | The cancel hook (section 10.2).                                                                      |
| `name[24]`                                     |                     | Debug name.                                                                                          |

### 5.1 Task flags (`TASK_F_*`)

| Flag                  | Bit | Spec concept                                                  |
| --------------------- | --- | ------------------------------------------------------------- |
| `TASK_F_BLOCKED`      | 0   | Blocked. Set and cleared by the blocking subsystem.           |
| `TASK_F_SUSPENDED`    | 1   | Suspended / created non-ready.                                |
| `TASK_F_PENDING_KILL` | 2   | Marked for destruction. Also set by a task on itself to exit. |
| `TASK_F_DYING`        | 3   | Destruction has begun. Wakes now fail.                        |

A task is _eligible_ when `BLOCKED`, `SUSPENDED` and `PENDING_KILL` are all clear (`eligible()` in `scheduler.c`).

### 5.2 Priority and affinity constants

- `SCHED_PRIO_LEVELS` = 10, `SCHED_PRIO_DEFAULT` = 5, `SCHED_PRIO_KERNEL` = 255, `SCHED_PRIO_INHERIT` = -1.
- `affinity_t`: `AFFINITY_NONE` (0), `AFFINITY_SOFT` (1), `AFFINITY_HARD` (2); `AFFINITY_INHERIT` = -1.
- `CPU_ANY` = `0xFFFFFFFF` means "no CPU preference".

### 5.3 Creation parameters

```c
typedef struct task_create_info {
	const char* name;
	task_entry_t entry;
	void* arg;
	int priority;        /* SCHED_PRIO_INHERIT or 0..9                        */
	int affinity;        /* AFFINITY_INHERIT or affinity_t                    */
	uint32_t cpu;        /* affinity target, CPU_ANY = creator's / inherited  */
	uint32_t flags;      /* TASK_CREATE_*                                     */
	uint32_t kflags;     /* KTASK_* (kernel tasks only)                       */
	uint32_t kquanta;    /* kernel tasks: quanta count, 1..64 (0 = 1)         */
	size_t stack_size;   /* 0 = SCHED_DEFAULT_STACK                           */
} task_create_info_t;
```

`TASK_INFO(name, fn, arg)` produces a starting value that inherits priority and affinity, has no CPU preference and the default stack.
Override fields on the result before calling `task_create()`. Note that inheriting from a soft-pinned creator pins the child too.

Creation flags (`flags`), the spec's "initial state" and "spawn variants":

| Flag                    | Bit | Meaning                                                                                                                                      |
| ----------------------- | --- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `TASK_CREATE_SUSPENDED` | 0   | Do not schedule until `task_resume()`.                                                                                                       |
| `TASK_CREATE_SIBLING`   | 1   | Parent = creator's parent.                                                                                                                   |
| `TASK_CREATE_DETACHED`  | 2   | No parent. Exempt from cascading destruction.                                                                                                |
| `TASK_CREATE_KERNEL`    | 3   | Kernel task: kernel priority, kernel queue, `kflags` apply.                                                                                  |
| `TASK_CREATE_OWN_ARG`   | 4   | `arg` was allocated with `kalloc`. The scheduler `kfree`s it when the task is destroyed. If `task_create()` fails, the caller still owns it. |

Kernel behavior flags (`kflags`), the spec's "scheduling behaviors" table:

| Flag                      | Bit | Spec behavior                                         |
| ------------------------- | --- | ----------------------------------------------------- |
| (none)                    |     | Next timeslice                                        |
| `KTASK_PREEMPT`           | 0   | Immediate preemption (kernel-preemption IPI)          |
| `KTASK_INTERLEAVE`        | 1   | Interleaved: runs only in the P0 slot, once per cycle |
| `KTASK_TAKEOVER`          | 2   | CPU takeover (also sends the preemption IPI)          |
| `KTASK_RUN_TO_COMPLETION` | 3   | No quanta timer while it runs                         |

### 5.4 Per-CPU state (`cpu_t`)

| Field(s)                                                    | Notes                                                                                                                                           |
| ----------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `self`, `id`                                                | **Must be the first two fields** (offsets 0 and 8). A port may read the current CPU's descriptor through a CPU-local register at these offsets. |
| `hw_id`                                                     | Hardware identifier (APIC id on x86). Set by the port.                                                                                          |
| `online`, `started`                                         | `started` is set once the CPU enters its scheduling loop.                                                                                       |
| `arch`                                                      | `struct arch_cpu`, the port's per-CPU data.                                                                                                     |
| `rq_lock`                                                   | Protects the lists below.                                                                                                                       |
| `rq[10]`, `kq`                                              | One `task_list_t` per priority level, and the kernel queue.                                                                                     |
| `nr_tasks`, `nr_unpinned`                                   | Atomic counters of queued user tasks, and of those with no affinity. Kernel tasks are never counted.                                            |
| `takeover`                                                  | Number of takeover kernel tasks alive on this CPU.                                                                                              |
| `current`, `idle`, `preempted`, `zombies`, `idle_task`      | Owner-only. `preempted` is the displaced-task slot.                                                                                             |
| `phase`, `rr_next`, `burst_prio`, `burst_left`, `kint_used` | Owner-only selection state (section 7).                                                                                                         |
| `slice_start_us`, `slice_len_us`                            | Current slice timing. `slice_len_us == 0` means no timer.                                                                                       |
| `steal_failures`, `cooldown_until_us`, `next_balance_us`    | Idle and balancing state.                                                                                                                       |
| `ctx_switches`, `timer_irqs`                                | Statistics, used by `sched_test` and `cpu_info`.                                                                                                |

`system_cpus[WALLOS_SYSTEM_MAX_CPU]` is a static array indexed by logical id, so it is valid (all zero) before any CPU is brought up.

### 5.5 Logical CPU ids

A CPU's _logical id_ is its index in `system_cpus`. The BSP is 0. How a running CPU finds its own `cpu_t` is the port's decision. The core only ever calls `cpu_current()`.

## 6. Synchronization

This implements the specification's "SMP Synchronization" rules.

### 6.1 Locks

`sched_lock_t` is a single `volatile uint32_t`. `sched_lock_irqsave()` saves and disables interrupts (via the port), then spins with an `exchange`, then a relaxed read loop with `arch_cpu_relax()`.
`sched_unlock_irqrestore()` releases with a release store, then restores the interrupt state. `sched_trylock_irqsave()` makes one attempt and restores interrupts if it fails.

There is no fairness guarantee. Hold times are short and bounded.

### 6.2 What's locked by what

| State                                                            | Mechanism                                                                                      |
| ---------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| `rq[]`, `kq` list links                                          | `cpu_t.rq_lock`                                                                                |
| `nr_tasks`, `nr_unpinned`                                        | Atomic. Updated only by `rq_add_locked()` / `rq_del_locked()` (under the lock), read lock-free |
| `task_t.flags`                                                   | Atomic `__atomic_*` operations                                                                 |
| `task_t.cpu`                                                     | Atomic store inside `rq_add_locked()`                                                          |
| `cpu_t.takeover`                                                 | Atomic add/sub                                                                                 |
| Idle mask                                                        | Array of 64-bit words, one bit per logical CPU. `fetch_or` / `fetch_and`                       |
| Id registry and parent/child tree                                | One global lock in `task.c`, the "tree lock"                                                   |
| `current`, `preempted`, `zombies`, selection state, timing state | Owning CPU only                                                                                |

### 6.3 Lock rules as implemented

- A CPU never holds two `rq_lock`s.
  - `sched_move_tasks()` trylocks the source, detaches into a private chain linked through `rq_next`, unlocks, then locks the destination.
- The tree lock and an `rq_lock` are never held together.
  - Task creation takes the tree lock, releases it, then takes the destination's `rq_lock`.
- The handler drops its own `rq_lock` before calling `sched_destroy_chain()`, `sched_redistribute()`, `sched_balance_periodic()` and `sched_idle_try_steal()`.
- IPIs are sent while holding the tree lock in `task_kill()` and in the cascade in `sched_task_destroy()`. This is safe because the target task of the IPI is held valid by that lock. An IPI send waits on the interrupt controller's delivery status, with interrupts off.

### 6.4 Idle mask operations (`balance.c`)

- `sched_set_targetable()` / `sched_clear_targetable()` read first and only write if the bit needs to change, so a busy CPU rarely touches the shared cache line.
- `sched_claim_idle_cpu(exclude)` walks the words, takes the lowest set bit that is not `exclude` and is below `sched_online`, and claims it with `fetch_and`. It wins only if the previous value had the bit set. Otherwise it tries the next bit.
- The idle mask is used by task creation (`choose_cpu()`), including kernel tasks. Balancing does not use it.

### 6.5 Wake versus destruction

`task_wake()` reads the task's `cpu` field _before_ the CAS that clears `BLOCKED`, and never dereferences the task after it.
It then calls `sched_kick()` on the saved CPU.
Together with `sched_task_destroy()` setting `DYING` with `fetch_or` and calling the cancel hook only if the _previous_ value had `BLOCKED`, exactly one side acts on a racing wake/destroy.

## 7. Selection

All of this runs under `rq_lock`, inside `select_locked()`.

### 7.1 Order

1. `pick_kernel()`, takes the first eligible kernel task. A task with `KTASK_INTERLEAVE` is only taken when `phase == 0` and `kint_used` is false (setting `kint_used`), or while taking over.
2. If the CPU is taken over, nothing else is selected.
3. The displaced task (`preempted`), if any. Resumed if eligible, reaped if pending-kill, otherwise put back at the **front** of its queue.
4. The P0 / P1 / normal flow.

### 7.2 Interleaving

`phase` is 0 (P0 slot), 1 (P1 slot) or 2 (normal slot).
Each attempt takes the slot's queue and advances `phase`: 0 to 1, 1 to 2, 2 to 0.
Up to three attempts are made per selection, which visits each slot once from any starting phase, so an eligible task is always found if one exists.
A selection from the normal slot leaves `phase == 0`, which is what makes P0 and P1 run between every normal task.
`kint_used` is cleared whenever a non-kernel task is selected.

### 7.3 Bursts

"Bursts" are calculated based on runqueue lengths (as defined in spec). The CPU struct keeps track of a current burst cycle with `burst_left`.
`burst_left > 0`: serve `burst_prio` again. If it has no eligible task the burst ends. Otherwise it goes through normal flow.

### 7.4 Reaping

`pick_from()` and `pick_kernel()` remove any task with `PENDING_KILL` they pass, blocked or not, and chain it on a caller-supplied list through `rq_next`.
Eligible tasks are removed and returned.
Blocked tasks are skipped in place.
Only the first eligible task is taken, so tasks behind it in the same queue are not examined that pass.

> This can mean that it can take a while to actually kill a task, if it's not encountered until a later time.
> This can be fixed if it's actually a problem in real usage. I didn't feel it was worth it when writing the code.

## 8. The Scheduling Handler

`sched_handle(cpu, ctx, reason)` returns the context to resume.

Only possible early returns (returning `ctx` unchanged):

- reason `IPI` when the CPU is not idle (including nothing running yet)
- reason `KPREEMPT` with nothing running
- reason `TIMER` before `started`

_**IMPORTANT:**_ The handler runs on the interrupted task's own stack. Destruction, including cancel hooks supplied by other subsystems, runs there too, so task stacks must have headroom beyond their own needs.

> There is very little statistic keeping in the scheduler. `ctx_switches` increments when the selected task differs from the outgoing one, and is basically the only debug information.

## 9. Balancing

Balancing follows the spec.

> P0/P1 tasks are not moved. This is in the spec.
> There shouldn't be enough of those P0/P1 tasks that they become unbalanced anyway.
> I just wanted to note this here so I would remember if it became a problem.

## 10. Task Lifecycle Implementation

### 10.1 Creation (`task_create`)

Creation follows the spec, and is mostly straightforward in the code.

The only note is that kernel takeovers don't have any way of notifying hard affinity tasks yet.
This is noted in a large TODO block.

### 10.2 Blocking

- `task_block_begin(t, cancel, ctx)`: stores the hook, then `fetch_or`s `BLOCKED` with release ordering. The hook is a function pointer and a context value stored in the TCB, the scheduler never interprets either.
- `sched_block_wait()`: yields while `BLOCKED` is set.
- `sched_block_current(cancel, ctx)`: both of the above on the current task.
- `task_wake(t)`: Returns false if `DYING` is set, true (a no-op) if not blocked.
- `task_wake_id(id)`: looks the task up under the tree lock and wakes it while holding that lock, so the task cannot be freed mid-wake. Intended for timer callbacks and interrupt context.

**Usage pattern** (this is race free, there is a chance of a lost wakeup otherwise):

```c
// waiter
lock(&dev->lock);
if (dev->ready) { unlock(&dev->lock); return; }          // re-check first
task_block_begin(sched_current(), dev_cancel, dev);      // flag set under the lock
waitlist_add(&dev->waiters, sched_current());
unlock(&dev->lock);
sched_block_wait();

// waker
lock(&dev->lock);
dev->ready = true;
task_t* t = waitlist_pop(&dev->waiters);                 // off the list first...
unlock(&dev->lock);
if (t) task_wake(t);                                     // ...and never touch t afterwards
```

`sched_block_current()` combines `task_block_begin` and `sched_block_wait` and is only safe when the waker cannot run before it is called.

The cancel hook must remove the task from the subsystem's waitlist and be idempotent.
It runs from the scheduling handler with interrupts off and no runqueue lock held.

### 10.3 Destruction

Always called by the owning CPU from the handler, with the task off every queue.

If the task was blocked, it will call the cancel hook. It cascades on all children of the task, sending a reschedule IPI to the child's CPU if it's a different CPU.
It will add the task to the CPUs zombie list, to ensure we aren't on a stack that we are freeing.

`sched_reap_zombies()` frees the stack (if `owns_stack`), the argument (if `owns_arg`) and the TCB at the start of the next handler entry on that CPU.

> Because the argument is freed here, a task that is killed before it ever runs does not leak it.

### 10.4 Exit, kill, suspend, resume

- `task_exit()` sets `PENDING_KILL` on the current task and yields forever (the first yield never returns, but is in a loop anyway to ensure nothing can return).
- `task_kill(id)`: tree lock, registry lookup, set `PENDING_KILL`, send a reschedule IPI to the owner if it is a different CPU (while still holding the lock), return whether the task existed.
- `task_suspend(id)` / `task_resume(id)`: set/clear `SUSPENDED` by id.

### 10.5 Boot thread adoption

`sched_adopt_boot_task(name)` creates a TCB for the running thread.
It has default priority, `AFFINITY_SOFT`, owning CPU = the current CPU, `owns_stack = false`.
It is registered, and becomes `current` of the BSP.
The current kernel entry path continues onto the kernel terminal.
This will eventually be changed, so the kernel terminal is it's own newly created task with it's own stack so we're no longer on the boot stack.

> The adopted boot task cannot exit via return. It must exit via `task_exit()`.

### 10.6 Bring-up

- `sched_init(n)` sets `sched_online`, builds each CPU's idle task (16 KiB stack, `sched_idle_main`, a context from `arch_task_context_init()`), marks the CPU online, then publishes `sched_ready` with release ordering.
- `sched_start_bsp()` sets `started`, the slice timing and the rebalance time, and arms the first quanta timer, all with interrupts off.
- `sched_ap_entry()` spins on `sched_ready` (acquire), masks interrupts, clears `current` (the bring-up context is thrown away), sets `started`, then yields. It never returns.
- `sched_task_bootstrap(fn, arg)` is where every new task starts. It calls `fn(arg)` and then `task_exit()`.

## 11. Public API Summary

| Call                                                             | Purpose                                                              |
| ---------------------------------------------------------------- | -------------------------------------------------------------------- |
| `sched_init(n)`                                                  | BSP: after all APs are up.                                           |
| `sched_adopt_boot_task(name)`                                    | BSP: turn the boot thread into a task.                               |
| `sched_start_bsp()`                                              | BSP: begin preemption.                                               |
| `sched_ap_entry()`                                               | APs: wait for `sched_init`, then enter the scheduler. Never returns. |
| `sched_current()`, `cpu_current()`, `cpu_get(id)`, `cpu_count()` | Introspection.                                                       |
| `sched_yield()`                                                  | Yield.                                                               |
| `task_create(info)`, `task_spawn(name, fn, arg)`                 | Create, return the id.                                               |
| `task_spawn_*(...)`                                              | Spawn a task with a common signature. (section 11.1)                 |
| `task_exit()`                                                    | Tells the scheduler to kill the calling task. Never returns.         |
| `task_kill(id)`                                                  | Request destruction given an id.                                     |
| `task_suspend(id)`, `task_resume(id)`                            | Make ineligible / eligible.                                          |
| `task_block_begin`, `sched_block_wait`, `sched_block_current`    | Blocking (section 10.2).                                             |
| `task_wake(t)`, `task_wake_id(id)`                               | Wake.                                                                |

### 11.1 Direct spawning for common function signatures

There are three helpers for spawning functions in the forms `void fn(void)`, `int fn(void)`, and `int fn(int, char**)`.

```c
task_spawn_void(TASK_NAMED("poll"), poll_loop);           // void fn(void)
task_spawn_int(TASK_INFO_PRIO("svc", 2), svc_main);       // int  fn(void)
task_spawn_main(TASK_NAMED("cmd"), cmd_main, argc, argv); // int  fn(int, char**)
```

The first argument is a `task_create_info_t`. There are some builders to simplify creation of the task info:

| Builder                                  | Result                                       |
| ---------------------------------------- | -------------------------------------------- |
| `TASK_NAMED(name)`                       | Inherit priority and affinity, default stack |
| `TASK_INFO_PRIO(name, prio)`             | Explicit priority                            |
| `TASK_INFO_PINNED(name, cpu, affinity)`  | Explicit affinity and target CPU             |
| `TASK_INFO_DETACHED(name)`               | No parent                                    |
| `TASK_INFO_KERNEL(name, kflags, quanta)` | Kernel task                                  |

`task_create_with(info, fn, arg)` starts a plain `void fn(void*)` with an info block, which covers any other function shape, a small `void fn(void*)` wrapper may need to be written.

- `void`/`int(void)`: the function pointer itself is passed as the task's argument. The scheduler does no allocation for these. An `int` result is discarded (there is no exit status yet).
- `int(int, char**)`: the `argv` array **and** its strings are copied into one `kalloc`'d block.
  - The block is handed over with `TASK_CREATE_OWN_ARG`, so the scheduler frees it when the task is destroyed. If `task_create()` fails, the spawner frees it.
  - The copy is capped at `TASK_ARGV_MAX_BYTES`.
  - A larger `argv`, a `NULL` entry, a negative `argc`, or `argc > 0` with a `NULL` `argv` fails the spawn with `TASK_ID_INVALID`.

## 12. Known Deviations and Gaps

These are places where the implementation does not (yet) match the specification, or where behavior is worth knowing.

| Item                              | Detail                                                                                                                                          |
| --------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| **Hard affinity takeover signal** | Not implemented (needs IPC). Marked `TODO` in `sched_redistribute()`.                                                                           |
| **Lost-wakeup**                   | `sched_block_current()` can lose a wake that arrives before it is called. Use the `task_block_begin` + `sched_block_wait` form for real events. |
| **Missed wake-up IPI**            | `sched_kick()` reads the target's `current` without a lock. A miss is bounded by the idle wake interval.                                        |
| **Kill latency**                  | Unbounded. I wrote this way in the spec, but could result in tasks taking a long time to kill.                                                  |
| **No sleep primitive**            | Timed waits need a subsystem plus a timer. I do plan on adding actual sleeps eventually.                                                        |
| **Task ids**                      | See comment in TCB (`task.h`).                                                                                                                  |
| **Exit Status**                   | Tasks have no way to indicate and exit status. I need to put more thought into it.                                                              |

## 13. Specification Traceability

| Specification section              | Where it lives                                                                                        |
| ---------------------------------- | ----------------------------------------------------------------------------------------------------- |
| Priority, quanta                   | `prio_quanta_us`, `quanta_for()` in `scheduler.c`                                                     |
| Fairness, priority 0/1             | `select_locked()`, `pick_normal()`                                                                    |
| Starvation mitigation              | `back_to_back()`                                                                                      |
| Runqueue balancing                 | `balance.c`                                                                                           |
| Kernel tasks, preemption, takeover | `pick_kernel()`, the handler, `task_create()`, `sched_redistribute()`                                 |
| Yielding                           | `sched_yield()` to `arch_yield()`                                                                     |
| Blocking, blocking handlers        | `task_block_begin()`, `task_wake*()`, `sched_block_*()`                                               |
| Task creation                      | `task_create()`, `sched_adopt_boot_task()`                                                            |
| Task destruction                   | `sched_task_destroy()`, `sched_destroy_chain()`, `sched_reap_zombies()`, `task_exit()`, `task_kill()` |
| Timer and quanta expiry            | `arch_timer_*` calls in `sched_handle()`                                                              |
| Scheduling handler, idle task      | `sched_handle()`, `sched_idle_main()`, `sched_init()`                                                 |
| SMP synchronization                | `sched_internal.h` (locks), `sched_move_tasks()`, section 6 above                                     |
| Idle state                         | `sched_idle_try_steal()`, the idle-mask functions                                                     |
| Platform requirements              | `arch.h`. See porting.md                                                                              |
