# WallOS "Fair Enough" Scheduler (WFES)

The WallOS "Fair Enough" Scheduler (WFES) is a priority-weighted, per-CPU task scheduler for SMP systems. It is built around simplicity and minimal cross-CPU coordination, favoring predictable, low-overhead behavior over strict fairness guarantees.

> Note: For the purposes of this document, `CPU` refers to a logical compute unit (threads on x86, cores on aarch64, etc.).

---

## Table of Contents

- [WallOS "Fair Enough" Scheduler (WFES)](#wallos-fair-enough-scheduler-wfes)
  - [Table of Contents](#table-of-contents)
  - [Philosophies](#philosophies)
  - [Affinity](#affinity)
  - [Priority](#priority)
    - [Fairness](#fairness)
      - [Priority 0 and 1](#priority-0-and-1)
        - [Intended Use and Quanta Cap](#intended-use-and-quanta-cap)
    - [Starvation Mitigation](#starvation-mitigation)
      - [Determining `D`](#determining-d)
      - [Hard Cap](#hard-cap)
      - [Examples](#examples)
  - [Runqueue Balancing](#runqueue-balancing)
    - [Stealing Tasks With Affinity](#stealing-tasks-with-affinity)
  - [Kernel Tasks](#kernel-tasks)
  - [Yielding](#yielding)
  - [Blocking](#blocking)
  - [Task Creation](#task-creation)
  - [Task Destruction](#task-destruction)
  - [Timer \& Quanta Expiry](#timer--quanta-expiry)
  - [SMP Synchronization](#smp-synchronization)
  - [Idle State](#idle-state)
    - [Entering Idle](#entering-idle)
    - [Cooldown Expiry](#cooldown-expiry)
    - [Receiving Tasks While Idle](#receiving-tasks-while-idle)
    - [Kernel Interaction](#kernel-interaction)
  - [Considerations](#considerations)

---

## Philosophies

The WallOS "Fair Enough" Scheduler is a priority-weighted, per-CPU scheduler designed for simplicity and scalability:

- Each CPU manages its own `cpu_rq` with no global runqueue, allowing each CPU to make scheduling decisions independently without synchronization overhead.
- There is no "main" processor. The BSP holds that role only until the scheduler is initialized and tasks are distributed.
- Priority is weighted, not just ordered. Each priority level receives half the runtime of the one above it.
- Priority 0 and 1 are a special "overriding" class, interleaved between every task transition. These queues are expected to be *mostly* empty under normal operation.
- Fairness is approximated, not guaranteed. Starvation is mitigated, but hard latency or throughput guarantees are not made for any given task.
- Affinity binds a task to a specific CPU. Soft affinity is almost always respected, but can be overridden to prevent a CPU from idling indefinitely. Hard affinity is absolute, and tasks using it must tolerate the possibility of never running under certain circumstances.
- Load is distributed lazily. CPUs steal or offer tasks periodically, or immediately when a CPU runs dry. Cross-CPU coordination is intentionally rare.
- Blocking is not the scheduler's responsibility. Subsystems that block tasks own the responsibility of re-queuing them.
- The kernel sits outside all priority rules and may preempt anything at any time. This is a deliberate escape hatch, not a routine mechanism.

---

## Affinity

Tasks can be set to only ever run on a single CPU, preventing them from being stolen.

By default, tasks are created **without** affinity.

There are three levels of affinity:

| Affinity Level | Description                                                                                                    |
|----------------|----------------------------------------------------------------------------------------------------------------|
| 0 - None       | The task is **not** set to run on exclusively one CPU.                                                         |
| 1 - Soft       | The task **is** set to run on exclusively one CPU, but **can** be moved to another if needed.                  |
| 2 - Hard       | The task **is** set to run on exclusively one CPU, and **cannot** be moved to another under any circumstances. |

Soft affinity is the recommended value for tasks that require affinity. It is incredibly rare for a soft affinity task to be moved to a different CPU. See [Stealing Tasks With Affinity](#stealing-tasks-with-affinity) for more information.

Hard affinity carries the risk that the task may never run if its CPU is taken over by the kernel. See [Kernel Tasks](#kernel-tasks) for more information.

---

## Priority

There are 10 priority levels, `0-9`. Priority 0 is the highest; priority 9 is the lowest. The default priority of a newly created task is 5.

Each priority level gets double the runtime of the next lower priority (with the exception of priority 0 and 1), expressed in units of `quantum`, assuming system timer resolution allows it. The amount of time a given task is allotted is called its `quanta`.

The default `quantum` is 16 ms of runtime.

| Priority     | Time (quanta) |
|--------------|---------------|
| Kernel (255) | Any           |
| 0            | 8             |
| 1            | 4             |
| 2            | 8             |
| 3            | 4             |
| 4            | 2             |
| 5            | 1             |
| 6            | 1/2           |
| 7            | 1/4           |
| 8            | 1/8           |
| 9            | 1/16          |

Kernel priority tasks are special tasks created by the kernel and are excluded from all other priority rules. They are [discussed below](#kernel-tasks). From the perspective of an end-user or application programmer, kernel tasks can be safely ignored.

### Fairness

With the exception of priority 0 and 1 tasks, all tasks are scheduled round-robin in descending priority order.

For example, the expected scheduling flow, where `P#` is the priority level:

```plaintext
P2 -> P3 -> P4 -> P5 -> P6 -> P7 -> P8 -> P9 -> P2 -> P3 -> ...
```

Upon a timeslice expiring or a task yielding, the task is placed at the back of its runqueue. If a given runqueue has no tasks, it is skipped.

Any newly added task, regardless of priority, is **not** immediately preemptive. It is added to the back of the runqueue corresponding to its priority level.

#### Priority 0 and 1

Priority 0 and 1 tasks are considered "overriding." Their presence means they **must** run if present. Upon the addition of a priority 0 or 1 task, the normal scheduling flow is interrupted and the task is inserted into the next available timeslot.

Priority 0 and 1 tasks are **not** immediately preemptive: they will run after the currently running task's quanta expires.

Priority 0 and 1 tasks are interleaved between other priority levels:

```plaintext
P0 -> P1 -> P2 -> P0 -> P1 -> P3 -> P0 -> P1 -> P4 -> ...
```

It is expected that very few, if any, tasks will ever occupy priority 0 or 1. Tasks at these levels are expected to use as little of their timeslice as possible.

##### Intended Use and Quanta Cap

Priority 0 and 1 deviate from the doubling pattern intentionally.
Following the pattern strictly would yield 512ms and 256ms respectively.
Runtimes this large would dominate the CPU and cause sluggishness in everything else, contradicting their purpose entirely.

**P0 and P1 are designed for low-latency work:** short bursts that need to run soon, not long computations that need a lot of time. Their quanta are capped at 8 and 4 respectively, matching the P2 ceiling, to enforce brevity.

> ***If a task genuinely needs more runtime than a P2 priority, it should likely be implemented as a [kernel task](#kernel-tasks).***

Because P0 and P1 are interleaved between every task transition, their effective CPU share is disproportionately high relative to their raw quanta.
Even at modest quanta values, a consistently occupied P0 or P1 runqueue can dominate scheduling.
The quanta cap is therefore a necessary constraint to keep the system functional under any load.

A well-behaved P0 or P1 task should yield well before its quanta expires.
Consistently consuming the full allotment is a strong signal that the task is misusing its priority level.
The scheduler does not enforce this, but it is a design expectation.

The scheduler makes no distinction between a task that uses 1ms of its quanta and one that uses the full allotment.
Both are treated identically at the scheduling level.
The responsibility for brevity lies entirely with the task.

---

### Starvation Mitigation

To prevent long runqueues from starving other tasks, the scheduler may run multiple tasks from the same priority level back-to-back.
This applies only to P2-P9.
P0 and P1 are unaffected, as their interleaving is unconditional.

Starvation mitigation does not alter priority ordering.
It only affects how many times a given priority level is selected before descending to the next.

The number of consecutive runs granted to a given priority level is:

```plaintext
back_to_back = clamp(floor(len(rq) / D), 1, 8)
```

Where:

- `len(rq)` is the number of runnable tasks in that priority's runqueue.
- `D` is a dynamic divisor determined by overall system load.
- `clamp(x, 1, 8)` ensures a minimum of 1 (normal behavior) and a hard maximum of 8 consecutive runs.

A result of 1 is the default behavior (no extra consecutive runs).
Starvation mitigation only meaningfully activates when the result exceeds 1.

#### Determining `D`

Only **non-empty** runqueues (P2-P9) are considered when computing `D`.
Runqueues with 0 tasks are excluded entirely, as they have no bearing on scheduling pressure.
Runqueues with fewer than 10 tasks are still included: they are short, but they still must be considered for contention.

`D` is then determined by how many of these non-empty runqueues contain 20 or more tasks:

- If **3 or fewer** non-empty runqueues have 20+ tasks:

  ```plaintext
  D = max(longest_other_rq, 10)
  ```

- If **more than 3** non-empty runqueues have 20+ tasks:

  ```plaintext
  D = max(floor(avg_rq_len), 10)
  ```

Where:

- `longest_other_rq` is the length of the largest non-empty runqueue, excluding the one currently being evaluated.
- `avg_rq_len` is the average length across all **non-empty** P2-P9 runqueues.
- The `max(..., 10)` term acts as a floor in both cases, treating 10 as the baseline normal runqueue length and preventing disproportionate results when all queues are short.

#### Hard Cap

`back_to_back` is capped at 8.

Without a cap, an extremely large runqueue could still produce an unreasonably large consecutive run count despite the dampening effect of `D`. The cap ensures that no single priority level can monopolize the CPU under extreme load.

The value 8 was chosen to:

- Provide meaningful starvation relief under heavy imbalance.
- Preserve responsiveness for other priority levels.

#### Examples

**Few long runqueues** (≤3 non-empty queues with 20+ tasks: use `max(longest_other, 10)` as `D`):

> For sake of brevity, `-> P0 -> P1 ->` will be replaced with `-> P01 ->`

Given:

```plaintext
P2=4, P3=2, P4=26, P5=34
```

Only P4 and P5 exceed 20, so `D = 10`:

```plaintext
P4: clamp(floor(26/10), 1, 8) = 2
P5: clamp(floor(34/10), 1, 8) = 3
```

Resulting flow:

```plaintext
P2 -> P01 -> P3 -> P01 -> P4 -> P01 -> P4 -> P01 -> P5 -> P01 -> P5 -> P01 -> P5 -> P01 -> P6 -> ...
```

**Many long runqueues** (>3 non-empty queues with 20+ tasks — use `max(floor(avg), 10)` as `D`):

Given:

```plaintext
P2=25, P3=30, P4=26, P5=34, P6=2, P7=2
```

P8 and P9 are empty and excluded. Four queues exceed 20, so:

```plaintext
avg = floor((25+30+26+34+2+2) / 6)
    = floor(119/6)
    = 19

D = 19
```

```plaintext
P2: clamp(floor(25/19), 1, 8) = 1
P3: clamp(floor(30/19), 1, 8) = 1
P4: clamp(floor(26/19), 1, 8) = 1
P5: clamp(floor(34/19), 1, 8) = 1
P6: clamp(floor(2/19),  1, 8) = 1
P7: clamp(floor(2/19),  1, 8) = 1
```

Note that when load is broadly distributed, the average rises and `D` rises with it, naturally dampening back-to-back counts across the board. Starvation mitigation is most aggressive when load is concentrated in one or two runqueues, and least aggressive when all runqueues are similarly loaded, which is the desired behavior.

Even under extreme imbalance (e.g., thousands of tasks in one runqueue), the clamp guarantees that no priority level will run more than 8 consecutive times before descending to the next.

---

## Runqueue Balancing

Other CPUs will occasionally steal tasks from other CPUs if their own runqueue is sufficiently empty. Similarly, a CPU may offer some of its tasks to another CPU if there is a significant imbalance in runqueue sizes.

Any stolen task is removed from the **back** of the source `cpu_rq`.

Rebalancing is only triggered periodically, on the order of every few seconds.

If a CPU completely runs out of tasks, it will immediately attempt to steal several tasks from other CPUs. A cooldown period follows, during which the CPU will not attempt to steal again.

### Stealing Tasks With Affinity

Soft affinity tasks may only be stolen when **all** of the following conditions are true:

- The stealing CPU has no other tasks of any priority.
- All other CPUs have *only* tasks with affinity.
- The CPU owning the target task has *only* other tasks with affinity.
- The target task is **not** the only task on its owning CPU.

Due to the specificity of these conditions, soft affinity tasks are very rarely stolen, and only to prevent a CPU from idling indefinitely.

**Tasks with hard affinity can never be stolen.**

---

## Kernel Tasks

The kernel reserves the right to immediately preempt any task of any priority. When this occurs, the preempted task is **not** moved to the end of the runqueue, and the runqueue position is **not** updated:

```plaintext
P2 -> P3 (preempted by kernel) -> PK -> Same P3 task -> P4 -> ...
```

Kernel tasks do not have to respect any priority rules.

Upon addition, a kernel task may do any of the following, though none are guaranteed:

- Run for any amount of quanta, up to 64 (1024 ms at default timing).
- Run between regular task transitions (similar to priority 0 and 1 interleaving).
- Immediately preempt the running task.
- Run in the next timeslice after the current task expires or yields.
- Run only once, from start to completion.
- Forcefully take over the CPU.

In the case of a CPU takeover, all current tasks are redistributed amongst the remaining CPUs, **excluding** tasks with hard affinity. Hard affinity tasks must be designed to tolerate the possibility of never running if the kernel takes over their CPU indefinitely. The kernel will provide some form of signal to such tasks when a takeover begins, and the task will be given a *very short* quanta to handle this notice. *(The signaling mechanism is yet to be determined - see [Considerations](#considerations).)*

There are no other cases where a task may be preempted by the addition of another task. All other preemptions occur only once the current task's quanta has expired.

---

## Yielding

Yielding keeps the task in its `cpu_rq` but requests that the CPU switch context to the next task before the yielding task's normal timeslice expires. This is intended to replace busy-waiting in non-blocking functions.

---

## Blocking

Tasks that are blocked are removed from the `cpu_rq` by the system function they are waiting on. For example, if a task is blocked by `getc(stdin)`, the handler responsible for returning a value to `getc()` manages its own queue of waiting tasks. When the next character arrives, the handler requests that the task be re-added to the `cpu_rq`.

This prevents the CPU from needing to manage blocked tasks directly.

---

## Task Creation

> **TODO:** Fill in this section.
>
> I need to talk about:
>
> - How a newly created task is assigned to a `cpu_rq`
> - How it and where tasks are created.
> - Initial state of a newly created task (ready, suspended, etc.).
> - How the task's priority and affinity are set at creation.
> - Any resource allocation (stack, TCB, etc.).

---

## Task Destruction

> **TODO:** Fill in this section.
>
> I need to talk about:
>
> - How tasks are "normally" removed from a runqueue upon "completion" (normal exit).
> - How the scheduler is notified of task destruction.
> - Resource cleanup responsibilities (who frees the stack, etc.).
> - Behavior if a task is destroyed while blocked.
> - How destruction can be requested externally (pkill), and how that interacts with a running task's quanta.

---

## Timer & Quanta Expiry

> **TODO:** Fill in this section.
>
> I need to talk about the following:
>
> - Timing method (APIC Timer, HPET, etc), including how it's handled (global, per cpu, etc)
> - The interrupt handler's responsibilities: saving context, invoking the scheduler, restoring context.
> - How lower priorities work if the timer resolution isn't good enough

---

## SMP Synchronization

> **TODO:** Fill in this section.
>
> I need to talk about the following:
>
> - How primitives are protected (likely atomics)
> - How cross-CPU communication is performed
> - Rebalancing protocol
> - Handling of races between task stealing and task destruction

---

## Idle State

When a CPU's `cpu_rq` becomes empty, it enters the idle state.

If the CPU has recently attempted to steal tasks, it enters a steal cooldown period.
During this time, it will not immediately attempt to steal again.
Instead, it marks itself as available to receive work by setting a bit in a global atomic mask indicating that it may be targeted by other CPUs for task donation.
This mask allows busy CPUs to proactively offer tasks to idle CPUs without requiring immediate cross-CPU coordination.
The idle CPU does not poll other CPUs directly during cooldown; it simply declares its availability and waits.

### Entering Idle

While idle, the CPU executes a low-power halt instruction (e.g., HLT on x86 or the platform equivalent).
The processor remains halted until the next interrupt.

The primary wake source is the periodic timer interrupt (typically at the same interval as the priority 9 quantum).

Upon each wake:

- The CPU updates its steal cooldown state.
- It checks whether any tasks have been added to its cpu_rq.
- It checks whether another CPU has offered tasks to it.
- If tasks are now available, normal scheduling resumes immediately.
- If no tasks are available and cooldown has expired, it attempts to steal tasks.
- If no tasks are obtained, it re-enters the halted state.

This ensures idle CPUs consume minimal power while still remaining responsive.

### Cooldown Expiry

Once the cooldown period expires, if the CPU still has no runnable tasks, it will actively attempt to steal tasks from other CPUs.

Before the stealing process begins, the targetable bit is cleared in the global idle mask to prevent concurrent donation during the steal attempt.

If stealing succeeds, the CPU resumes normal scheduling.

If stealing fails, a new cooldown period begins and the CPU re-enters idle after resetting the targetable bit.

### Receiving Tasks While Idle

A CPU that is marked as targetable may receive tasks from another CPU at any time.
When tasks are offered:

- The donating CPU inserts tasks directly into the idle CPU's `cpu_rq`.
- The idle CPU is awakened via an interrupt mechanism (e.g., IPI), if required.
- The targetable bit is cleared once work is received.

This allows work distribution to occur opportunistically without requiring a fully synchronized rebalance operation.

### Kernel Interaction

Kernel tasks may preempt an idle CPU at any time, independent of steal cooldown or idle state rules.

Idle CPUs are the preferred target for new kernel tasks. If any CPU is idle, a kernel task will prefer it over preempting a running task.

When a kernel task targets an idle CPU:

- The CPU is awakened via interrupt (e.g., IPI).
- The targetable bit is cleared in the global idle mask.
- The kernel task begins execution immediately.

The CPU does not re-enter the normal wake loop, the kernel task takes over directly.

If no CPUs are idle, the kernel falls back to its normal preemption behavior as described in [Kernel Tasks](#kernel-tasks).

---

## Considerations

The following are known areas subject to future change:

- **Rebalancing frequency** is intentionally vague. It is not yet known how often rebalancing is required or how long it takes, given that it requires co-operation between all CPUs. Timing is subject to change.
- **Blocking task re-insertion** does not currently receive any special treatment. Tasks are placed at the back of the runqueue. A common pattern is to "boost" unblocked tasks, since they have likely been waiting a long time. This could be achieved by inserting at the front of the runqueue rather than the back, but may not prove necessary.
- **Low quanta tasks** (priority 8 and 9, at 2 ms and 1 ms respectively) may have a significant portion of their runtime consumed by context switching. This is not necessarily a problem, as tasks intentionally placed in these tiers are likely doing very little work. With the default priority of 5 providing 16x more runtime than priority 9, very few tasks should ever occupy these tiers unintentionally.
- **Starvation algorithm granularity** could be improved. If a P5 runqueue has 1000 tasks and P2 has 1, P5 tasks will still feel disproportionately slow relative to P2. This is partially by design, but priority boosting could be explored if it becomes a real problem in practice.
- **Hard affinity takeover signals** are not yet defined. There is currently no IPC implementation in WallOS. This will be revisited once a default IPC protocol is established.
- **Idle state race condition**: When a CPU becomes idle and its "Targetable" bit is set, there could be a race condition where multiple busy CPUs try to "Offer" tasks to it at the exact same millisecond. This will be fixed by using an atomic "Compare-and-Swap" on that targetable bit so only one donor wins the right to push tasks.
- **Priority Inheritance** may be needed if, for example, a P0 task is relying on something from a P9 task. That P9 task may need to be briefly granted the same priority as the P0 task that needs it.
