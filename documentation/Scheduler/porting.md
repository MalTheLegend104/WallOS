# WFES Porting Guide

This document explains how to port the WallOS "Fair Enough" Scheduler (WFES) to a new architecture, and records the decisions each existing port made.

- For _how the scheduler should behave_, read the specification.
- For _how the architecture independent core works_, read [implementation.md](implementation.md).

The core (`klibc/scheduler/scheduler.c`) contains no architecture specific code and should not need changes for a port.
If a port seems to require one, treat that as a gap in the architecture interface, and make changes to the interface below.

## 1. What a Port Provides

A port is a header that `klibc/include/scheduler/arch.h` includes, plus its own source files.

`arch.h` selects it with a preprocessor test:

```c
#if defined(__x86_64__)
#include <x86_64/arch_cpu.h>
#else
#error "WFES: no architecture port for this target"
#endif
```

> Architecture specific code will likely be changed around in terms of handling later.
> Point above still stands, but the ports header placement will likely change.

### 1.1 `struct arch_cpu`

Per-CPU data owned by the port, embedded in `cpu_t` as the `arch` member.
The core never looks inside it.

### 1.2 Inline functions

There are some functions that must be defined `static inline` in the port header, because they are on hot paths:

| Signature                         | Function                                                                                                          |
| --------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| `cpu_t* arch_cpu_self(void)`      | The calling CPU's `cpu_t`. Must work from interrupt context.                                                      |
| `uint32_t arch_cpu_id(void)`      | The calling CPU's logical id (its index in `system_cpus`).                                                        |
| `uint64_t arch_irq_save(void)`    | Disable interrupts on this CPU; return an opaque value describing the previous state.                             |
| `void arch_irq_restore(uint64_t)` | Re-enable interrupts **only if** the saved state had them enabled.                                                |
| `void arch_cpu_relax(void)`       | Spin-wait hint.                                                                                                   |
| `void arch_halt(void)`            | Wait for an interrupt, in low power. Called by the idle task with interrupts enabled, returns after an interrupt. |

> Some of these mimic things already present in `<arch>/arch.h`, the `cpu_*` functions.
> Most of these scheduler ones (at least for x86_64) are better versions.
> The `<arch>/arch.h` versions will likely be updated soon...
> The architecture port is still required to cover both, as these are scheduler exclusive versions, and should be used only in the scheduler.

### 1.3 Ordinary functions

These can be defined in a `.c` file, they just need to be discoverable at link time.
The are not in a hot path like those in above.

<!-- This table is ugly, I tried my best... -->

| Signature                                                                                       | Function                                                                                                                                                                                                                                      |
| ----------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `uint64_t arch_time_us(void)`                                                                   | Monotonic microseconds. Cheap (called several times per scheduling pass). Only compared against the same CPU's earlier readings, but should be consistent across CPUs because tasks migrate.                                                  |
| `uint32_t arch_timer_min_us(void)`                                                              | The shortest interval the timer can reliably deliver. The core clamps shorter requests up to it.                                                                                                                                              |
| `void arch_timer_arm_oneshot_us(uint32_t us)`                                                   | Arm this CPU's timer to fire **once**, `us` microseconds from now, replacing any pending expiry. The expiry enters the scheduling handler with reason `TIMER`. Called from the handler with interrupts disabled.                              |
| `void arch_timer_disarm(void)`                                                                  | Cancel any pending expiry on this CPU.                                                                                                                                                                                                        |
| `void arch_send_resched_ipi(uint32_t cpu_id)`                                                   | Make that CPU enter the handler with reason `IPI`. Asynchronous, carries no data. May be called with interrupts on or off.                                                                                                                    |
| `void arch_send_kpreempt_ipi(uint32_t cpu_id)`                                                  | Same, with reason `KPREEMPT`. **Must work when the target is the calling CPU** (kernel tasks that preempt immediately are announced this way even from the creating CPU).                                                                     |
| `void arch_yield(void)`                                                                         | Enter the handler on the calling CPU, now, with reason `YIELD`. Must work with interrupts disabled (the AP entry path calls it that way), and the interrupt state the task resumes with comes from its saved context, not from the call site. |
| `void* arch_task_context_init(void* stack_base, size_t stack_size, task_entry_t fn, void* arg)` | Build a context for a new task. See section 3.3.                                                                                                                                                                                              |

### 1.4 What the core provides to the port

| Function                                                           | Contract                                                                                                                                         |
| ------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| `void* sched_handle(cpu_t* cpu, void* ctx, sched_reason_t reason)` | The scheduling handler. Pass the saved context, resume the context it returns. Returning `ctx` itself means "keep running what was interrupted". |
| `void sched_task_bootstrap(task_entry_t fn, void* arg)`            | Where every new task begins (via the context you build). Never returns.                                                                          |

> `sched_reason_t` is `SCHED_REASON_TIMER`, `_YIELD`, `_IPI` or `_KPREEMPT`.

## 2. CPU Identity and `cpu_t`

The core calls `cpu_current()`, which is `arch_cpu_self()`.

Implementation of this is very architecture dependent. It must be constant-time and safe in an interrupt context.

If you use a CPU-local register, `cpu_t` is laid out so that two fields can be read at fixed offsets: `self` at offset 0 (a pointer to the `cpu_t` itself) and `id` at offset 8.

At bring-up, before `sched_init()` runs, the port must have filled in for every CPU that is going to be used:

- `system_cpus[n].self`, `.id` (= `n`), `.hw_id`, `.online`
- its `arch` data (for example, timer calibration)
- whatever per-CPU register or table `arch_cpu_self()` reads

Logical ids must be contiguous from 0 (the BSP is 0).
Assign them in the order CPUs start, so a CPU that fails to start leaves no hole.

Provide a way to derive the logical id from the hardware id.

> It is recommended to not depend on the per-CPU register (if architecture is implemented that way), it can be needed before it's set or needed from other CPUs.

## 3. Context Switching

### 3.1 The model

A **context** is a `void*` that only the port understands.

The core stores it in the task (`saved_ctx`) when the task is switched out and passes it back when the task is selected again.

The trap that enters the handler does this:

```
entry (reason):
    save the full interrupted state on the interrupted task's stack
    acknowledge the interrupt, for timer and IPI entries (not for a software yield)
    ctx = sched_handle(this_cpu, saved_state_pointer, reason)
    switch to the stack / state that ctx identifies
    restore the full state and return from the interrupt
```

The core only requires that `ctx` identifies enough to resume.

> A convenient design (used by x86_64) is for the context to be the stack pointer of a register frame pushed on the task's own stack. "Switching" is then just loading that stack pointer.

### 3.2 Requirements on the entry path

- The handler is entered **with interrupts disabled** and must run to completion with them disabled. Resuming restores the interrupt state from the context.
- **Acknowledge the interrupt controller before calling the handler** for timer and IPI entries. The handler may not return to the code that was interrupted, so an acknowledgment placed after the call may never happen.
- The handler runs on whatever stack the trap leaves it on, normally the interrupted task's own stack.
  - Destruction (including other subsystems' cancel hooks) can run there, so task stacks need headroom for the handler.
  - If your port switches to a per-CPU interrupt stack, that works too, saved state still has to live in storage that belongs to the task.
- Keep the saved-frame layout used by the trap and by `arch_task_context_init()` identical.
- Timers and IPIs are asynchronous. The handler can be entered for `TIMER` shortly after a task yielded. The core re-arms or disarms the timer on every pass, so a stale expiry is harmless.

### 3.3 `arch_task_context_init`

Build, on the new task's stack, a context that when first resumed:

1. runs on that stack, with the stack aligned as it would be at function entry after a call (including whatever fake return address the ABI needs)
2. has interrupts **enabled**
3. calls `sched_task_bootstrap(fn, arg)`

Return the context handle.

The stack memory is allocated by the core, the port only fills it in.

## 4. Timer, Clock and IPIs

- **One-shot timer.** The core arms it after every scheduling decision and disarms it for tasks without a quanta limit. If the hardware only has a periodic timer, emulate one-shot behavior inside the port (for example, count ticks).
- **Resolution.** Report the real minimum in `arch_timer_min_us()`. Quanta shorter than that (P8 and P9 are 2 ms and 1 ms by default) are clamped up.
- **Clock.** Needs to be monotonic and cheap. It is read at the start of every handler pass and again when arming the timer.
- **IPIs.** Two distinct deliveries to a given CPU. The reschedule IPI is allowed to be dropped when it races with the target going idle (the idle wake interval bounds the delay). The kernel-preemption IPI should not be dropped, though the core survives it (the kernel task then simply waits for the next scheduling boundary).
  - If sending an IPI takes more than one register write, make the sequence atomic with respect to interrupts on the sending CPU.

## 5. Bring-up

The order the core expects:

1. **BSP, early:**
   1. initialize the clock
   2. bind the BSP as logical CPU 0 (section 2)
   3. install the entry points for the timer, yield and both IPIs in the shared interrupt table **before any other CPU starts** (they share it)
   4. calibrate this CPU's timer and store the result in its `arch` data
2. **Each AP, as it starts:**
   1. set up its interrupt controller
   2. bind itself as the next logical id
   3. calibrate its timer
   4. signal that it is started
   5. enable interrupts
   6. call `sched_ap_entry()`
      - It waits here until the BSP finishes step 3, then enters the scheduler and never returns.
3. **BSP, once all APs are up (or timed out):**
   1. `sched_init(cpu_count)`
   2. `sched_adopt_boot_task(name)`
   3. `sched_start_bsp()`
      - After this the BSP is preemptible and tasks may be created. Start any system services after this point.

Things that can go wrong:

- An AP calling `sched_ap_entry()` before its CPU identity is bound
- Interrupt entry points installed after an AP has already loaded the shared table
- The BSP calling `sched_start_bsp()` before `sched_adopt_boot_task()`. With nothing running on the BSP, the first timer tick cannot save a context

## 6. Porting Checklist

- [ ] `arch_cpu.h` with `struct arch_cpu` and the six inline functions
- [ ] a source file with the ordinary functions, and `arch_task_context_init`
- [ ] trap entries for timer, yield, reschedule IPI and kernel-preemption IPI, each ending in a switch to the context returned by `sched_handle`
- [ ] CPU identity bound for every CPU, with contiguous logical ids
- [ ] clock and per-CPU timer calibration
- [ ] bring-up sequence in section 5 wired into the architecture's CPU start-up code
- [ ] `arch.h` updated to include your header
- [ ] a "port decisions" section added to this document, covering the headings used in section 7

---

## 7. Port Decisions: x86_64

Files: `x86_64/arch_cpu.h`, `x86_64/sched_arch.c`, `x86_64/sched_stubs.asm`.

### 7.1 CPU identity

- **Per-CPU register:** both `IA32_GS_BASE` and `IA32_KERNEL_GS_BASE` point at the CPU's `cpu_t`. `arch_cpu_self()` reads `%gs:0`; `arch_cpu_id()` reads `%gs:8`.
  - This is kind of a gap. We don't really want `cpu_t*` available from userspace. This will be updated to do `swapgs` on a transition from kernel to user (and vise versa) eventually.
- **Lookup without GS:** `x86_cpu_id_from_apic(apic_id)` uses a 256-entry table (`0xFFFF` means unknown).
  - APIC ids above 255 (x2APIC) are not supported. This may be an issue I solve eventually.
- **Logical ids:** an atomic counter hands them out in start order. BSP first (`x86_percpu_bsp_bind`), then each AP (`x86_percpu_ap_bind`). APs start one at a time, so ids are contiguous even if one fails.
- **`struct arch_cpu`:** `lapic_freq_hz`, `lapic_ticks_per_ms`.

### 7.2 Context

A context is the address of a saved register frame on the task's own stack. Lowest address first:

```
r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax   (15 GPRs)
reason                                                      (pushed by the per-vector stub)
rip cs rflags rsp ss                                        (pushed by the CPU)
```

That is 21 quadwords (168 bytes).
In 64-bit mode the CPU always pushes `ss:rsp`, even without a privilege change.
The C view of it is `x86_regs_t` in `sched_arch.c`, the order is fixed by the push sequence in `sched_stubs.asm`.

> Note that this can clobber FPU state. I do plan on fixing this, but the kernel really doesnt use floats anywhere right now.

**New task frame** (`arch_task_context_init`):

- `top = (stack_base + size) & ~0xF`, then `top -= 8` and store 0 there.
  - This is a fake return address, so `rsp % 16 == 8` at entry, as after a `call`
- The frame sits just below that, at `top - sizeof(frame)`
- `rip = sched_task_bootstrap`, `rdi = fn`, `rsi = arg`, `rsp = top`
- `rflags = 0x202` (IF set)
- `cs` and `ss` are copied from the kernel (read once in `x86_sched_arch_early_init`), since the GDT layout belongs to the rest of the kernel.

### 7.3 Entry stubs

Four stubs (timer, yield, reschedule IPI, kernel-preemption IPI) each do `push qword <reason>; jmp x86_sched_common`.
The common code pushes the 15 GPRs, passes `rsp` as the argument, aligns `rsp` to 16, clears the direction flag, calls `x86_sched_dispatch`, loads the returned pointer into `rsp`, pops the registers, drops the reason, and finally `iretq`s.

`x86_sched_dispatch`:

| Reason                 | EOI                           | Handler reason          |
| ---------------------- | ----------------------------- | ----------------------- |
| Timer                  | yes (and counts `timer_irqs`) | `SCHED_REASON_TIMER`    |
| Reschedule IPI         | yes                           | `SCHED_REASON_IPI`      |
| Kernel-preemption IPI  | yes                           | `SCHED_REASON_KPREEMPT` |
| Yield (software `int`) | **no**                        | `SCHED_REASON_YIELD`    |

### 7.4 Vectors

| Vector | Use                                    |
| ------ | -------------------------------------- |
| `0xE0` | Yield (software interrupt, `int 0xE0`) |
| `0xE1` | Reschedule IPI                         |
| `0xE2` | Kernel-preemption IPI                  |
| `0xEF` | LAPIC timer                            |

All four are installed by `x86_sched_register_vectors()` as DPL 0 interrupt gates (type `0x8E`, no IST), so the CPU clears IF on entry.
They go into the same IDT that all CPUs share, which is why registration must precede AP start.

The disabled legacy PIC is remapped to `0xF0-0xFF`. This is why the IPI vectors were put at `0xE0-0xE2`, and the timer at `0xEF`.

> The timer is more important then the rest, so in the case it gets two at the same time, the higher will win.

### 7.5 Timer

The LAPIC timer in one-shot mode, divide-by-16.
Arming writes the divide config (`0x3`), then the LVT timer register with just the vector (bits 17:18 clear selects one-shot, mask clear), then the initial count, which starts it:

```c
ticks = lapic_ticks_per_ms * us / 1000        // clamped to [1, 0xFFFFFFFF]
```

Disarming writes an initial count of 0.
`lapic_ticks_per_ms` is calibrated per CPU against the TSC (BSP in `arch_init_cpus`, each AP in `x86_ap_main`).

`arch_timer_min_us()` returns 50, a conservative floor, the scheduler doesnt care as long as it's below 1000, which any modern CPU should be.

### 7.6 Clock

`rdtsc` divided by `tsc_per_us = tsc_freq / 1000000` (integer division, so the clock is very slightly fast if the TSC frequency is not a whole number of MHz).
Assumes an **invariant, synchronized TSC**.

> There is zero support for variant TSCs, it will just treat them as invariant, with a warning to the screen and serial.
> This will likely result in timing being incorrect, but will be "consistently" incorrect.
> Virtually all CPUs have invariant TSC, I just wanted to note this.

### 7.7 IPIs

Through the LAPIC interrupt command register using physical destination mode and fixed delivery.
Write the destination APIC id (shifted into bits 24-31) to ICR high, then `vector | (1 << 14)` (assert) to ICR low.
The sender first waits for the delivery-status bit (bit 12) to clear, and disables interrupts around the two writes so an interrupt handler on the same CPU cannot interleave its own IPI.
Sending to oneself works with an explicit destination.

### 7.8 Halt

Simply `sti; hlt`, nothing special.

### 7.9 Potential Issues

- **No red zone**: interrupts land on task stacks and would corrupt it.
  - The kernel itself is built with `-mno-red-zone`. Any user code I build will also likely have `-mno-red-zone`.
  - This can be an issue if new code is built for the OS though, and can be relatively easy to fix. Subtracting the red zone from rsp before saving context and adding it back before the `iretq` should be enough to fix it, and wouldn't effect things that don't have the redzone.
- **No FPU, SSE or AVX in kernel code:** none of that state is saved on a context switch.
  - I didn't want to deal with this when writing it. This is a problem, this will need to be fixed.
- **Ring 0 only:** there is no stack switching on privilege change (no TSS `RSP0`/IST use) and no segment register saving.
  - Adding user mode needs `swapgs`, TSS stack handling and segment state in the frame.
  - I need to do a lot of other small things in the kernel before we can do user space anyway, this is probably the last thing I'll do before adding user mode.
- **NMI and machine-check** are not handled specially.

### 7.10 Interactions with other x86 code

- **PIT:** it is borrowed during TSC and AP bring-up and drives the system uptime tick afterwards. `pit_reset(0)` reprograms it. Uptime does not advance while the PIT is borrowed (meaning that at most a second or two is removed from system runtime).
- **Legacy IRQs:** once the PIC is disabled, ISA IRQs reach the CPUs through the IOAPIC, routed to the BSP at vector `32 + irq`, so device handlers keep working.
  - The legacy PIC is entirely disabled (masked). It is rerouted to `0xF0-0xFF`, `0xFF` is used for the lapic suprious interrupts as well.
  - In theory none of this should get through to the system and this range can be reused, the IDT does not change the interrupts those are routed to, so they all route to the default (doesn't apply to `0xFF`).

---

## 8. Template for New Ports

Add a section to this document in this form:

```
## Port Decisions: <architecture>

Files: ...

### CPU identity       how arch_cpu_self()/arch_cpu_id() work, how logical ids are assigned
### Context            what a context is; the frame layout; what arch_task_context_init builds
### Entry path         how the four reasons are entered, where the interrupt is acknowledged
### Interrupt vectors  or equivalent: what is used and why
### Timer              hardware used, how one-shot is achieved, minimum interval, calibration
### Clock              source, assumptions
### IPIs               mechanism, delivery rules
### Halt               how halting is handled
### Potential Issues   any caveats about the implementation
### Bring-up           where the section 5 steps are wired in
### Interactions       anything else in the platform that the port touches
```
