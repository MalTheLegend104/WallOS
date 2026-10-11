#include <scheduler/scheduler.h>
#include <string.h>

#include <wallos_attributes.h>

#include <x86_64/lapic.h>

#include <system/cpuid.h>
#include <system/gdt.h>
#include <system/idt.h>


/* Layout MUST match the push order in sched_stubs.asm (lowest address first) */
typedef struct x86_regs {
	uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
	uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
	uint64_t reason; // pushed by the per-vector stub, removed by x86_sched_common
	uint64_t rip, cs, rflags, rsp, ss; // CPU-pushed iretq frame
} x86_regs_t;

// Saved context = [ctx+0] pointer to the x86_regs_t above
// [ctx+CTX_HDR] the FPU/SSE/AVX save area
// MUST MATCH sched_stubs.asm
#define CTX_HDR 64

enum {
	X86_REASON_TIMER,
	X86_REASON_YIELD,
	X86_REASON_RESCHED,
	X86_REASON_KPREEMPT,
	X86_REASON_SYSCALL,
};

extern void x86_sched_timer_entry(void);
extern void x86_sched_yield_entry(void);
extern void x86_sched_resched_entry(void);
extern void x86_sched_kpreempt_entry(void);
extern void x86_syscall_entry(void);
extern void x86_iret_user(uint64_t rip, uint64_t rsp) __attribute__((noreturn));
extern void x86_fpu_reset(void);

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Per-CPU
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------


/* GS convention
 *     kernel mode: GS_BASE = this CPU's cpu_t*, KERNEL_GS_BASE = current task's user GS
 *     user mode:   GS_BASE = current task's user GS, KERNEL_GS_BASE = this CPU's cpu_t*
 *
 * The interrupt stubs swapgs if the interrupt came from ring 3
 * The regular interrupt stubs automatically handle this, the scheduler stubs need a lot of special handling.
 */
#define MSR_EFER           0xC0000080u
#define MSR_STAR           0xC0000081u
#define MSR_LSTAR          0xC0000082u
#define MSR_SFMASK         0xC0000084u
#define MSR_FS_BASE        0xC0000100u
#define MSR_GS_BASE        0xC0000101u
#define MSR_KERNEL_GS_BASE 0xC0000102u

// These should probably be in another header somewhere...

static inline void wrmsr64(uint32_t msr, uint64_t v) {
	__asm__ volatile("wrmsr" ::"c"(msr), "a"((uint32_t) v), "d"((uint32_t) (v >> 32)) : "memory");
}
static inline uint64_t rdmsr64(uint32_t msr) {
	uint32_t lo, hi;
	__asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
	return ((uint64_t) hi << 32) | lo;
}
static inline uint64_t read_cr(int n) {
	uint64_t v;
	switch (n) {
		case 0:  __asm__ volatile("mov %%cr0, %0" : "=r"(v)); break;
		case 3:  __asm__ volatile("mov %%cr3, %0" : "=r"(v)); break;
		default: __asm__ volatile("mov %%cr4, %0" : "=r"(v)); break;
	}
	return v;
}
static inline void write_cr(int n, uint64_t v) {
	switch (n) {
		case 0:  __asm__ volatile("mov %0, %%cr0" ::"r"(v) : "memory"); break;
		case 3:  __asm__ volatile("mov %0, %%cr3" ::"r"(v) : "memory"); break;
		default: __asm__ volatile("mov %0, %%cr4" ::"r"(v) : "memory"); break;
	}
}

static uint16_t apic_to_logical[256]; /* 0xFFFF means unknown */
static volatile uint32_t next_logical = 0;
static uint64_t tsc_per_us = 1;
static uint16_t kernel_cs, kernel_ss;
static uint64_t kernel_cr3;

/* These are all read by sched_stubs.asm (and enable_features.asm) */
uint8_t x86_fpu_use_xsave = 0;
uint8_t x86_fpu_have_avx = 0;
uint64_t x86_fpu_area_size = 512;
uint64_t x86_user_cs = X86_SEL_USER_CS;
uint64_t x86_user_ss = X86_SEL_USER_DS;
static uint64_t x86_xcr0;

void x86_fpu_cpu_init(void) {
	uint64_t cr0 = read_cr(0);
	cr0 &= ~((1ull << 2) | (1ull << 3)); // Clear EM (emulation) and TS (task-switched)
	cr0 |= (1ull << 1) | (1ull << 5); // Set MP (monitor coprocessor) and NE (native exceptions)
	write_cr(0, cr0);

	// Enable OS support for SSE instructions and their exception handling
	uint64_t cr4 = read_cr(4);
	// OSFXSR: enable FXSAVE/FXRSTOR and SSE state support
	// OSXMMEXCPT: enable unmasked SSE exceptions to raise #XM
	cr4 |= (1ull << 9) | (1ull << 10);
	if (x86_fpu_use_xsave) cr4 |= (1ull << 18); // OSXSAVE: allow the OS to manage extended processor state with XSAVE
	write_cr(4, cr4);

	if (x86_fpu_use_xsave) {
		// Configure which processor state components are enabled for XSAVE/XRSTOR
		__asm__ volatile("xsetbv" ::"c"(0), "a"((uint32_t) x86_xcr0), "d"((uint32_t) (x86_xcr0 >> 32)));
	}

	x86_fpu_reset();
}

static void fpu_global_init(void) {
	uint32_t a, b, c, d;

	// Check CPU features for XSAVE support (CPUID leaf 1, ECX bit 26)
	__cpuid_count(1, 0, a, b, c, d);
	bool xsave = c & (1u << 26);

	// Default to the legacy FPU/SSE save area size used by FXSAVE
	x86_fpu_area_size = 512;

	if (xsave) {
		// Query which extended processor state components can be managed by XSAVE
		__cpuid_count(0xD, 0, a, b, c, d);
		uint64_t supported = a | ((uint64_t) d << 32);

		// Enable x87 and SSE state, plus AVX if supported
		// Don't enable AVX-512 or AMX because we want the task state to be small
		x86_xcr0 = supported & 0x7;

		// x87 and SSE state are required for this configuration
		// Fall back to FXSAVE if either component is unavailable (which it shouldn't be but you never know)
		if (!(x86_xcr0 & 1) || !(x86_xcr0 & 2)) xsave = false;
	}

	x86_fpu_use_xsave = xsave;
	x86_fpu_have_avx = xsave && (x86_xcr0 & 4);

	// Enable the selected FPU features and configure XCR0 before querying the enabled XSAVE area's required size
	x86_fpu_cpu_init();

	if (xsave) {
		// CPUID leaf 0xD, subleaf 0 reports the required size in EBX for the state components currently enabled in XCR0
		// Round up to a 64-byte boundary for XSAVE-area alignment
		__cpuid_count(0xD, 0, a, b, c, d);
		x86_fpu_area_size = (b + 63u) & ~63u;
	}
}

static void syscall_cpu_init(void) {
	wrmsr64(MSR_EFER, rdmsr64(MSR_EFER) | 1u); // Enable the SYSCALL/SYSRET instruction pair in EFER.SCE

	// STAR[47:32] is the kernel CS selector used by SYSCALL
	// STAR[63:48] is the SYSRET selector base:
	//    CS = base + 16 (user code, 64-bit)
	//    SS = base + 8  (user data)
	// SYSRET derives both user selectors from this base
	// Subtracting 8 from the user data selector makes SS match X86_SEL_USER_DS and CS match the user code selector immediately following it
	uint64_t sysret_base = (uint64_t) ((X86_SEL_USER_DS & ~3u) - 8u);
	wrmsr64(MSR_STAR, (sysret_base << 48) | ((uint64_t) kernel_cs << 32));

	// Set the 64-bit kernel entry point invoked by SYSCALL.
	wrmsr64(MSR_LSTAR, (uint64_t) (uintptr_t) x86_syscall_entry);

	// Configure the RFLAGS bits that syscall clears on entry
	// We prevent user code from entering the kernel with:
	//     - TF (0x00100): Trap Flag, suppress single-step tracing
	//     - IF (0x00200): Interrupt Flag, disable maskable interrupts
	//     - DF (0x00400): Direction Flag, ensure string operations run forward
	//     - NT (0x04000): Nested Task Flag (shouldnt be possible but still disable)
	//     - AC (0x40000): Alignment Check flag
	wrmsr64(MSR_SFMASK, 0x100 | 0x200 | 0x400 | 0x4000 | 0x40000);
}

static void bind_cpu(uint32_t apic_id, uint32_t logical) {
	cpu_t* c = &system_cpus[logical];
	c->self = c;
	c->id = logical;
	c->hw_id = apic_id;
	c->online = true;
	apic_to_logical[apic_id & 0xFF] = (uint16_t) logical;

	// Kernel GS = this CPU
	// The "user" half starts at 0 so a ring 3 task never sees a kernel pointer (it's replaced by the task's own user GS on every switch to a user task)
	wrmsr64(MSR_GS_BASE, (uint64_t) (uintptr_t) c);
	wrmsr64(MSR_KERNEL_GS_BASE, 0);

	c->arch.cur_cr3 = read_cr(3);
	x86_fpu_cpu_init(); // idempotent. BSP already did it, APs need it before their first trap
	syscall_cpu_init();
}

void x86_percpu_bind_tss(volatile uint64_t* rsp0_field) {
	arch_cpu_self()->arch.tss_rsp0 = rsp0_field;
}

uint32_t x86_cpu_id_from_apic(uint32_t apic_id) {
	uint16_t v = apic_to_logical[apic_id & 0xFF];
	return v == 0xFFFF ? CPU_ANY : v;
}

void x86_sched_arch_early_init(uint64_t tsc_freq_hz) {
	memset(apic_to_logical, 0xFF, sizeof apic_to_logical);
	tsc_per_us = tsc_freq_hz / 1000000ull;
	if (!tsc_per_us) tsc_per_us = 1;

	uint16_t cs, ss;
	__asm__ volatile("mov %%cs, %0" : "=r"(cs));
	__asm__ volatile("mov %%ss, %0" : "=r"(ss));
	kernel_cs = cs;
	kernel_ss = ss;
	kernel_cr3 = read_cr(3);

	fpu_global_init();
}

void x86_percpu_bsp_bind(uint32_t apic_id) {
	bind_cpu(apic_id, __atomic_fetch_add(&next_logical, 1, __ATOMIC_SEQ_CST));
}

uint32_t x86_percpu_ap_bind(uint32_t apic_id) {
	// APs start one at a time, so ids stay contiguous even if an AP fails
	uint32_t id = __atomic_fetch_add(&next_logical, 1, __ATOMIC_SEQ_CST);
	bind_cpu(apic_id, id);
	return id;
}

void x86_sched_register_vectors(void) {
	// These are raw gates, they have to do their own handling with swapgs and frame
	add_interrupt_handler_asm(X86_VEC_TIMER, x86_sched_timer_entry, 0, 0x8E);
	add_interrupt_handler_asm(X86_VEC_YIELD, x86_sched_yield_entry, 0, 0x8E);
	add_interrupt_handler_asm(X86_VEC_RESCHED, x86_sched_resched_entry, 0, 0x8E);
	add_interrupt_handler_asm(X86_VEC_KPREEMPT, x86_sched_kpreempt_entry, 0, 0x8E);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Timing
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

uint64_t arch_time_us(void) {
	uint32_t lo, hi;
	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return (((uint64_t) hi << 32) | lo) / tsc_per_us;   /* assumes invariant TSC */
}

uint32_t arch_timer_min_us(void) { return 50; }

void arch_timer_arm_oneshot_us(uint32_t us) {
	cpu_t* c = arch_cpu_self();
	uint64_t ticks = c->arch.lapic_ticks_per_ms * (uint64_t) us / 1000ull;
	if (!ticks) ticks = 1;
	if (ticks > 0xFFFFFFFFull) ticks = 0xFFFFFFFFull;

	lapic_write(LAPIC_DIVIDE_CONFIG, 0x3);              /* /16, matches calibration */
	lapic_write(LAPIC_LVT_TIMER, X86_VEC_TIMER);        /* bits 17:18 = 0 => one-shot */
	lapic_write(LAPIC_INITIAL_COUNT, (uint32_t) ticks); /* starts counting */
}

void arch_timer_disarm(void) { lapic_write(LAPIC_INITIAL_COUNT, 0); }

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// IPI/Yield
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------


static void send_ipi(uint32_t logical, uint8_t vector) {
	uint32_t apic = system_cpus[logical].hw_id;
	uint64_t f = arch_irq_save();       /* ICR high/low must not be interleaved */
	while (lapic_read(LAPIC_ICR_LOW) & (1u << 12)) arch_cpu_relax();
	lapic_write(LAPIC_ICR_HIGH, apic << 24);
	lapic_write(LAPIC_ICR_LOW, (uint32_t) vector | (1u << 14));  /* fixed, assert */
	arch_irq_restore(f);
}
void arch_send_resched_ipi(uint32_t cpu) { send_ipi(cpu, X86_VEC_RESCHED); }
void arch_send_kpreempt_ipi(uint32_t cpu) { send_ipi(cpu, X86_VEC_KPREEMPT); }

void arch_yield(void) {
	__asm__ volatile("int %0" ::"i"(X86_VEC_YIELD) : "memory");
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Context Creation
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

void* arch_task_context_init(void* stack_base, size_t stack_size, task_entry_t fn, void* arg) {
	uint64_t top = ((uint64_t) (uintptr_t) stack_base + stack_size) & ~0xFull;
	top -= 8; /* rsp%16 == 8 at entry, like after a call */

	uint64_t regs_addr = top - sizeof(x86_regs_t);
	uint64_t ctx_addr = (regs_addr - x86_fpu_area_size - CTX_HDR) & ~63ull;
	if (top <= (uint64_t) (uintptr_t) stack_base || ctx_addr < (uint64_t) (uintptr_t) stack_base + 512) return NULL; /* leave the task some room to run */

	*(uint64_t*) top = 0; /* fake return address */

	x86_regs_t* r = (x86_regs_t*) regs_addr;
	memset(r, 0, sizeof *r);
	r->rip = (uint64_t) (uintptr_t) sched_task_bootstrap;
	r->cs = kernel_cs;
	r->ss = kernel_ss;
	r->rflags = 0x202; /* IF=1 */
	r->rsp = top;
	r->rdi = (uint64_t) (uintptr_t) fn;
	r->rsi = (uint64_t) (uintptr_t) arg;

	// Header + a valid "reset state" FPU image
	// XSTATE_BV = 0 (all zero header) puts every component in its init state on xrstor
	// FCW and MXCSR are written explicitly
	// MXCSR is always loaded from memory and must be valid, and fxrstor reads both
	memset((void*) ctx_addr, 0, CTX_HDR + x86_fpu_area_size);
	*(uint64_t*) ctx_addr = regs_addr;
	uint8_t* area = (uint8_t*) ctx_addr + CTX_HDR;
	*(uint16_t*) (area + 0) = 0x037F;
	*(uint32_t*) (area + 24) = 0x1F80;
	return (void*) ctx_addr;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Task switch / user mode
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static inline uint64_t kstack_top(const task_t* t) {
	return ((uint64_t) (uintptr_t) t->stack_base + t->stack_size) & ~0xFull;
}

static void set_trap_stack(cpu_t* c, const task_t* t) {
	// A trap from ring 3 (interrupt or syscall) lands at the top of this task's kernel stack.
	// A task that is in user mode never has live kernel frames, so "top" is always free.
	// The adopted boot task has no stack record and never goes to ring 3.
	if (!t->stack_base) return;
	uint64_t top = kstack_top(t);
	c->arch.syscall_kstack = top;
	if (c->arch.tss_rsp0) *c->arch.tss_rsp0 = top;
}

void arch_task_switch(task_t* out, task_t* next) {
	cpu_t* c = arch_cpu_self();

	set_trap_stack(c, next);

	// We are in kernel mode here (the stub already swapgs'd if needed) so KERNEL_GS_BASE is the outgoing task's user GS.
	if (out && out->arch.user_mode) {
		out->arch.user_gs = rdmsr64(MSR_KERNEL_GS_BASE);
		out->arch.user_fs = rdmsr64(MSR_FS_BASE);
	}
	if (next->arch.user_mode) {
		wrmsr64(MSR_KERNEL_GS_BASE, next->arch.user_gs);
		wrmsr64(MSR_FS_BASE, next->arch.user_fs);
	}

	// Kernel tasks run on the kernel's page tables, so a dead user address space is never left loaded on a CPU.
	uint64_t want = next->arch.cr3 ? next->arch.cr3 : kernel_cr3;
	if (c->arch.cur_cr3 != want) {
		write_cr(3, want);
		c->arch.cur_cr3 = want;
	}
}

void arch_enter_user(uintptr_t entry, uintptr_t user_sp, uint64_t cr3) {
	arch_irq_save(); // stay masked until the iretq in x86_iret_user. Never restored on purpose
	cpu_t* c = arch_cpu_self();
	task_t* t = c->current;

	t->arch.user_mode = true;
	t->arch.cr3 = cr3;
	if (cr3 && c->arch.cur_cr3 != cr3) {
		write_cr(3, cr3);
		c->arch.cur_cr3 = cr3;
	}

	// swapgs inside x86_iret_user swaps these
	// GS_BASE <- user GS, KERNEL_GS_BASE <- cpu_t*
	wrmsr64(MSR_KERNEL_GS_BASE, t->arch.user_gs);
	wrmsr64(MSR_FS_BASE, t->arch.user_fs);

	set_trap_stack(c, t);
	x86_fpu_reset();
	x86_iret_user(entry, user_sp);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Syscalls
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#include <memory/kernel_alloc.h>
#include <syscall/syscall.h>

// ABI: number in rax, args rdi rsi rdx r10 r8 r9, result in rax
// rcx and r11 are clobbered by hardware
static void x86_syscall(x86_regs_t* regs) {
	arguments_t* syscall_args = (arguments_t*) kcalloc(1, sizeof(arguments_t));
	syscall_args->syscall_num = regs->rax;
	syscall_args->arg0 = regs->rdi;
	syscall_args->arg1 = regs->rsi;
	syscall_args->arg2 = regs->rdx;
	syscall_args->arg3 = regs->r10;
	syscall_args->arg4 = regs->r8;
	syscall_args->arg5 = regs->r9;

	syscall_dispatch(syscall_args);

	regs->rax = syscall_args->ret;
	regs->r9 = syscall_args->arg5;
	regs->r8 = syscall_args->arg4;
	regs->r10 = syscall_args->arg3;
	regs->rdx = syscall_args->arg2;
	regs->rsi = syscall_args->arg1;
	regs->rdi = syscall_args->arg0;
	kfree(syscall_args);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Dispatch
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

void* x86_trap_dispatch(x86_regs_t* regs, void* ctx) WALLOS_USED;
void* x86_trap_dispatch(x86_regs_t* regs, void* ctx) {
	sched_reason_t reason;

	switch (regs->reason) {
		case X86_REASON_TIMER:
			lapic_write(LAPIC_EOI, 0);
			arch_cpu_self()->timer_irqs++;
			reason = SCHED_REASON_TIMER;
			break;
		case X86_REASON_RESCHED:
			lapic_write(LAPIC_EOI, 0);
			reason = SCHED_REASON_IPI;
			break;
		case X86_REASON_KPREEMPT:
			lapic_write(LAPIC_EOI, 0);
			reason = SCHED_REASON_KPREEMPT;
			break;
		case X86_REASON_SYSCALL:
			// Runs as an ordinary preemptible kernel context on the task's own kernel stack.
			// The task can be preempted, blocked, and even resumed on a different CPU in here, so no cpu_t* may be held across the call.
			__asm__ volatile("sti" ::: "memory");
			x86_syscall(regs);
			__asm__ volatile("cli" ::: "memory");
			return ctx;
		default: /* software int, no EOI */
			reason = SCHED_REASON_YIELD;
			break;
	}
	return sched_handle(arch_cpu_self(), ctx, reason);
}