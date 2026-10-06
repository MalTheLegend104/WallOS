#include <scheduler/scheduler.h>
#include <string.h>

#include <system/idt.h>
#include <wallos_attributes.h>
#include <x86_64/lapic.h>

/* Layout MUST match the push order in sched_stubs.asm (lowest address first) */
typedef struct x86_regs {
	uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
	uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
	uint64_t reason; // pushed by the per-vector stub, removed by x86_sched_common
	uint64_t rip, cs, rflags, rsp, ss; // CPU-pushed iretq frame
} x86_regs_t;

enum {
	X86_REASON_TIMER,
	X86_REASON_YIELD,
	X86_REASON_RESCHED,
	X86_REASON_KPREEMPT
};

extern void x86_sched_timer_entry(void);
extern void x86_sched_yield_entry(void);
extern void x86_sched_resched_entry(void);
extern void x86_sched_kpreempt_entry(void);

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Per-CPU
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------


// TODO: I wrote this assuming we were staying ring3 without swapping to user space
// I need to change this around slightly so we swap the GS along with swapping from user to kernel (and back)

#define MSR_GS_BASE        0xC0000101u
#define MSR_KERNEL_GS_BASE 0xC0000102u

static inline void wrmsr64(uint32_t msr, uint64_t v) {
	__asm__ volatile("wrmsr" ::"c"(msr), "a"((uint32_t) v), "d"((uint32_t) (v >> 32)));
}

static uint16_t apic_to_logical[256]; /* 0xFFFF means unknown */
static volatile uint32_t next_logical = 0;
static uint64_t tsc_per_us = 1;
static uint16_t kernel_cs, kernel_ss;

static void bind_cpu(uint32_t apic_id, uint32_t logical) {
	cpu_t* c = &system_cpus[logical];
	c->self = c;
	c->id = logical;
	c->hw_id = apic_id;
	c->online = true;
	apic_to_logical[apic_id & 0xFF] = (uint16_t) logical;

	// TODO: Both the bases point at the CPU stuct
	// When we do userspace we want the user GS to hold something different
	wrmsr64(MSR_GS_BASE, (uint64_t) (uintptr_t) c);
	wrmsr64(MSR_KERNEL_GS_BASE, (uint64_t) (uintptr_t) c);
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
	typedef void (*h_t)(struct interrupt_frame*);
	add_interrupt_handler(X86_VEC_TIMER, (h_t) x86_sched_timer_entry, 0, 0x8E);
	add_interrupt_handler(X86_VEC_YIELD, (h_t) x86_sched_yield_entry, 0, 0x8E);
	add_interrupt_handler(X86_VEC_RESCHED, (h_t) x86_sched_resched_entry, 0, 0x8E);
	add_interrupt_handler(X86_VEC_KPREEMPT, (h_t) x86_sched_kpreempt_entry, 0, 0x8E);
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
	*(uint64_t*) top = 0; /* fake return address */

	x86_regs_t* r = (x86_regs_t*) (top - sizeof(x86_regs_t));
	memset(r, 0, sizeof *r);
	r->rip = (uint64_t) (uintptr_t) sched_task_bootstrap;
	r->cs = kernel_cs;
	r->ss = kernel_ss;
	r->rflags = 0x202; /* IF=1 */
	r->rsp = top;
	r->rdi = (uint64_t) (uintptr_t) fn;
	r->rsi = (uint64_t) (uintptr_t) arg;
	return r;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Dispatch
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

void* x86_sched_dispatch(x86_regs_t* regs) WALLOS_USED;
void* x86_sched_dispatch(x86_regs_t* regs) {
	cpu_t* c = arch_cpu_self();
	sched_reason_t reason;

	switch (regs->reason) {
		case X86_REASON_TIMER:
			lapic_write(LAPIC_EOI, 0);
			c->timer_irqs++;
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
		default: /* software int, no EOI */
			reason = SCHED_REASON_YIELD;
			break;
	}
	return sched_handle(c, regs, reason);
}
