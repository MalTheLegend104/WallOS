#include <scheduler/cpu.h>

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

#include <acpi/acpi_api.h>
#include <x86_64/lapic.h>
#include <system/idt.h>
#include <memory/virtual_mem.h>

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

	// We just disable the PIC in general
	// We use the PIT timer before we start the SMP setup
	// After this we'll just use the APIC timer
	disablePIC();


}