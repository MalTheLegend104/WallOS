#ifndef GDT_H
#define GDT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

	struct gdt_entry {
		uint16_t limit_low;
		uint16_t base_low;
		uint8_t  base_middle;
		uint8_t  access;
		uint8_t  granularity;
		uint8_t  base_high;
	} __attribute__((packed));

	struct gdt_entry_high {
		uint32_t base_upper;
		uint32_t reserved;
	} __attribute__((packed));

	struct gdt_ptr {
		uint16_t limit;
		uint64_t base;
	} __attribute__((packed));

	struct tss_64 {
		uint32_t reserved0;
		uint64_t rsp0;      // Stack pointer for Ring 0
		uint64_t rsp1;
		uint64_t rsp2;
		uint64_t reserved1;
		uint64_t ist[7];    // Interrupt Stack Table
		uint64_t reserved2;
		uint16_t reserved3;
		uint16_t iopb_offset;
	} __attribute__((packed));

	void set_ap_gdt_and_tss();
#ifdef __cplusplus
}
#endif
#endif