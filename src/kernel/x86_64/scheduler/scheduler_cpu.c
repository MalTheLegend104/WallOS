#include <scheduler/cpu.h>
#include <stdio.h>


#include <acpi/acpi_api.h>
#include <x86_64/lapic.h>
#include <system/idt.h>
#include <memory/virtual_mem.h>

void idle_task_main() {
	while (1) {
		__asm__ volatile("hlt");
	}
}

cpu_t system_cpus[WALLOS_SYSTEM_MAX_CPU];

cpu_t* cpu_current(void) {

}

cpu_t* cpu_get(uint32_t cpu_id) {

}

uint32_t cpu_count(void) {

}

volatile uint32_t ap_started_count = 0;
volatile uint32_t ap_last_id = 0;

// These should be pointers to the labels in your assembly
extern uint64_t pml4_pointer;
extern uint64_t ap_stack_pointer;
extern volatile uint32_t ap_ready_flag; // A flag the AP sets in ap_main

void x86_ap_main() {
	// Verify we have a working stack
	volatile uint64_t stack_test = 0xDEADBEEFCAFEBABEULL;

	// Get our APIC ID
	uint32_t apic_id = lapic_read(0x20) >> 24;

	// Record it
	ap_last_id = apic_id;

	// Signal BSP we are alive
	__atomic_fetch_add(&ap_started_count, 1, __ATOMIC_SEQ_CST);

	// Halt forever
	WALLOS_CLI_HLT();
}

void wakeup_ap(uint8_t apic_id, uint8_t vector) {
	// 1. Select the target AP (High 32-bits of ICR)
	// Destination is in bits 24-31
	lapic_write(LAPIC_ICR_HIGH, (uint32_t) apic_id << 24);

	// 2. Send INIT IPI
	// Level = Assert (bit 14), Delivery Mode = INIT (0b101)
	lapic_write(LAPIC_ICR_LOW, 0x0000C500);

	// Wait 10ms - Use your calibrated PIT or TSC timer
	delay_ms(10);

	// 3. Send SIPI #1
	// Delivery Mode = Startup (0b110), Vector = 0x08 (for 0x8000)
	lapic_write(LAPIC_ICR_LOW, 0x0000C600 | vector);

	// Wait 200us
	delay_us(200);

	// 4. Send SIPI #2 (Just to be sure)
	lapic_write(LAPIC_ICR_LOW, 0x0000C600 | vector);
}

// This exists for the original IDT and PIC setup
// We're going to reuse it here.
extern void disablePIC();

void arch_init_cpus() {
	// We need to get the APIC from ACPI.
	// We need to init the LOCAL APIC for the BSP before we can touch anything else.
	// To init the local APIC:
	// - disable timer
	// - fully mask the PIC
	// - enable APIC globally
	// - map the LAPIC mmio
	// - Program SVR
	/* Setup LVT entries (initially mask them)
		| Register  | Offset |
		| --------- | ------ |
		| LVT Timer | 0x320  |
		| LINT0     | 0x350  |
		| LINT1     | 0x360  |
		| Error     | 0x370  |
	*/
	// - set the Task Priority Register (TPR)
	// - Send EOI to LAPIC EOI register (0xB0)
	// - Setup LAPIC Timer
	// - Discover APs
	// - APs start in real mode
	// 		Trampoline must:
	// 		- Disable interrupts
	// 		- Setup temporary GDT
	// 		- Enable protected mode
	// 		- Jump to 32-bit entry
	// 		- Enable paging
	// 		- Jump to 64-bit long mode
	// 		- Call ap_main()
	// Send INIT-SIPI-SIPI
	// 		- Wait until ICR idle
	// 		- Send INIT IPI (Wait 10ms)
	// 		- Send SIPI (wait 200us)
	// 		- Send SIPI again.
	// Each AP should:
	// 		- Enable LAPIC (same MSR step)
	// 		- Setup its own stack
	// 		- Setup GDT/IDT
	// 		- Enable interrupts (all of them can use the same one for the basic 32 interrupts, nothing in there touches memory yet.)
	// 		- Signal BSP (atomic increment or flag)

	MADTTable* madt = get_madt();
	uintptr_t virt_lapic_base = mapSequentialKernelPagesWithFlags(1, madt->lapic_base, BIT_SIZE | BIT_WRITE | BIT_PRESENT | BIT_PCD);

	set_lapic_base((uint32_t*) virt_lapic_base);

	enable_lapic_msr(madt->lapic_base);

	// We just disable the PIC in general
	// We use the PIT timer before we start the SMP setup
	// After this we'll just use the APIC timer
	disablePIC();

	// This abstracts away a LOT of writing to LAPIC registers
	// This 
	bsp_init_lapic();

	uint64_t tsc_freq = get_tsc_freq();
	printf_color(PRINT_COLOR_RED, PRINT_DEFAULT_BG, "TSC FREQ: %d\n", tsc_freq);
	if (!tsc_freq) WALLOS_CLI_HLT();

	uint64_t lapic_timer_freq = calibrate_lapic_timer_with_tsc(tsc_freq);

	// Need to set up APIC timer
	uint32_t bsp_apic_id = lapic_read(LAPIC_ID) >> 24;
	for (uint32_t i = 0; i < madt->entry_count; i++) {
		MADTEntry* e = &madt->entries[i];

		if (e->type != 0) continue;
		if (!(e->lapic.flags & 1)) continue;
		if (e->lapic.apic_id == bsp_apic_id) continue;

		// This is an AP
	}
}