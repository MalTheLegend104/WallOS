#ifndef WALLOS_X86_64_ARCH_CPU_H
#define WALLOS_X86_64_ARCH_CPU_H
#include <scheduler/task.h>
#include <stdint.h>

struct arch_cpu {
	/* MUST stay first. sched_stubs.asm reads these through GS (cpu_t.arch sits at offset 16, see cpu.h) */
	uint64_t syscall_kstack; /* gs:16 kernel stack top of the task running here (syscall entry + RSP0) */
	uint64_t syscall_scratch; /* gs:24 user rsp put here by x86_syscall_entry */

	uint64_t lapic_freq_hz; /* LAPIC timer Hz at divide-by-16 */
	uint64_t lapic_ticks_per_ms;
	volatile uint64_t* tss_rsp0; /* points at this CPU's TSS.rsp0 */
	uint64_t cur_cr3; /* CR3 currently loaded here */
};

/* Vectors. 0xF0-0xFF are inside the remapped PIC range. */
#define X86_VEC_YIELD    0xE0
#define X86_VEC_RESCHED  0xE1
#define X86_VEC_KPREEMPT 0xE2
#define X86_VEC_TIMER    0xEF

/* These are much better than the arch.h header I wrote a while ago. Will probably change that one (and merge it a bit). */

/* Scheduler behavior requires these being static inlined, in the header. */
static inline cpu_t* arch_cpu_self(void) {
	cpu_t* c;
	__asm__ volatile("movq %%gs:0, %0" : "=r"(c));
	return c;
}

static inline uint32_t arch_cpu_id(void) {
	uint32_t id;
	__asm__ volatile("movl %%gs:8, %0" : "=r"(id));
	return id;
}

static inline uint64_t arch_irq_save(void) {
	uint64_t f;
	__asm__ volatile("pushfq\n\tpopq %0\n\tcli" : "=r"(f)::"memory");
	return f;
}

static inline void arch_irq_restore(uint64_t f) {
	if (f & (1u << 9)) __asm__ volatile("sti" ::: "memory");
}

static inline void arch_cpu_relax(void) {
	__asm__ volatile("pause" ::: "memory");
}

static inline void arch_halt(void) {
	__asm__ volatile("sti; hlt" ::: "memory");
}

void x86_sched_arch_early_init(uint64_t tsc_freq_hz); /* BSP, once, first */
void x86_percpu_bind_tss(volatile uint64_t* rsp0_field); /* All, where its TSS.rsp0 lives */
void x86_fpu_cpu_init(void); /* All, CR0/CR4/XCR0 */
void x86_percpu_bsp_bind(uint32_t apic_id); /* BSP gets assigned logical 0 */
uint32_t x86_percpu_ap_bind(uint32_t apic_id); /* AP, returns logical */
void x86_sched_register_vectors(void); /* BSP, before APs */
uint32_t x86_cpu_id_from_apic(uint32_t apic_id); /* GS-independent */
#endif // WALLOS_X86_64_ARCH_CPU_H