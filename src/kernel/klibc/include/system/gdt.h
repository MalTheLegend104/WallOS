#ifndef GDT_H
#define GDT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

	typedef struct gdt_entry {
		uint16_t limit_low;
		uint16_t base_low;
		uint8_t base_middle;
		uint8_t access;
		uint8_t granularity;
		uint8_t base_high;
	} __attribute__((packed)) gdt_entry_t;

	typedef struct gdt_entry_high {
		uint32_t base_upper;
		uint32_t reserved;
	} __attribute__((packed)) gdt_entry_high_t;

	typedef struct gdt_ptr {
		uint16_t limit;
		uint64_t base;
	} __attribute__((packed)) gdt_ptr_t;

	typedef struct tss_64 {
		uint32_t reserved0;
		uint64_t rsp0;      // Stack pointer for Ring 0
		uint64_t rsp1;
		uint64_t rsp2;
		uint64_t reserved1;
		uint64_t ist[7];    // Interrupt Stack Table
		uint64_t reserved2;
		uint16_t reserved3;
		uint16_t iopb_offset;
	} __attribute__((packed)) tss_64_t;

	/* Selector layout.
	 * SYSRET derives the user CS/SS selectors from IA32_STAR: with STAR_base = 0x10, SYSRET loads:
	 *     CS = STAR_base + 16 = 0x20 | RPL3 = 0x23
	 *     SS = STAR_base +  8 = 0x18 | RPL3 = 0x1B
	 *
	 * sched_arch.c uses these values when programming STAR and defining X86_SEL_USER_CS/X86_SEL_USER_DS
	 */
#define GDT_SEL_KCODE 0x08
#define GDT_SEL_KDATA 0x10
#define GDT_SEL_UDATA 0x18
#define GDT_SEL_UCODE 0x20
#define GDT_SEL_TSS   0x28 /* 16 byte descriptor, two slots */
#define GDT_SLOTS     7

// Used when setting up the CPUs. Sets the USER bits of the fields above.
#define X86_SEL_USER_DS (GDT_SEL_UDATA | 3)
#define X86_SEL_USER_CS (GDT_SEL_UCODE | 3)

	/**
	 * @brief Builds and loads this CPU's GDT and TSS.
	 * Returns the TSS so the caller can use it for x86_percpu_bind_tss().
	 */
	tss_64_t* set_gdt_and_tss(void);
#ifdef __cplusplus
}
#endif
#endif
