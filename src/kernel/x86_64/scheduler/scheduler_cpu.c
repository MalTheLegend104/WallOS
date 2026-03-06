#include <stdio.h>
#include <stdarg.h>

#include <scheduler/cpu.h>

#include <acpi/acpi_api.h>
#include <x86_64/lapic.h>
#include <system/idt.h>
#include <memory/virtual_mem.h>
#include <memory/kernel_alloc.h>

#include <x86_64/ioapic.h>
#include <system/timing.h>

void idle_task_main() {
	while (1) {
		__asm__ volatile("hlt");
	}
}

cpu_t system_cpus[WALLOS_SYSTEM_MAX_CPU];

cpu_t* cpu_current(void) { }
cpu_t* cpu_get(uint32_t cpu_id) { }
uint32_t cpu_count(void) { }

volatile int print_lock = 0;
void safe_printf(const char* format, ...) {
	// Basic Spinlock
	while (__atomic_test_and_set(&print_lock, __ATOMIC_ACQUIRE));

	va_list arg;
	va_start(arg, format);
	vprintf(format, arg);
	va_end(arg);

	__atomic_clear(&print_lock, __ATOMIC_RELEASE);
}

volatile int serial_print_lock = 0;
void safe_printf_serial(const char* format, ...) {
	// Basic Spinlock
	while (__atomic_test_and_set(&print_lock, __ATOMIC_ACQUIRE));

	va_list arg;
	va_start(arg, format);
	vprintf_serial(format, arg);
	va_end(arg);

	__atomic_clear(&print_lock, __ATOMIC_RELEASE);
}

// BSP is already started
volatile uint32_t ap_started_count = 1;
volatile uint32_t ap_stack_locked = 0;

void x86_ap_main() {
	__atomic_store_n(&ap_stack_locked, 0, __ATOMIC_SEQ_CST);

	// Verify we have a working stack
	volatile uint64_t stack_test = 0xDEADBEEFCAFEBABEULL;

	// Get our APIC ID
	uint32_t apic_id = lapic_read(0x20) >> 24;


	safe_printf("  [AP %d] Hello from the other side!\n", apic_id);
	safe_printf_serial("[SMP] AP %d reporting for duty. Stack is at %p\r\n", apic_id, &stack_test);

	// Signal BSP we are alive
	__atomic_fetch_add(&ap_started_count, 1, __ATOMIC_SEQ_CST);

	// Halt forever
	WALLOS_CLI_HLT();
}

// This exists for the original IDT and PIC setup
// We're going to reuse it here.
extern void disablePIC();

void pic_disable(void) {
	// Remap PIC to vectors 0xF0+ so stray interrupts don't hit CPU exceptions
	// Master: vectors 0xF0-0xF7, Slave: 0xF8-0xFF
	outb(0x20, 0x11); outb(0xA0, 0x11);  // ICW1: init
	outb(0x21, 0xF0); outb(0xA1, 0xF8);  // ICW2: new vector offsets
	outb(0x21, 0x04); outb(0xA1, 0x02);  // ICW3: cascade
	outb(0x21, 0x01); outb(0xA1, 0x01);  // ICW4: 8086 mode

	// Now mask everything on both PICs
	outb(0x21, 0xFF);
	outb(0xA1, 0xFF);
}

void arch_init_cpus() {
	// We need to get the APIC from ACPI.

	MADTTable* madt = get_madt();
	uintptr_t virt_lapic_base = mapSequentialKernelPagesWithFlags(1, madt->lapic_base, BIT_SIZE | BIT_WRITE | BIT_PRESENT | BIT_PCD);
	printf_serial("[SMP] Found MADT at %p, identifying CPUs...\r\n", madt);

	set_lapic_base((uint64_t*) virt_lapic_base);

	enable_lapic_msr(madt->lapic_base);

	WALLOS_CLI();

	// We just disable the PIC in general
	// We use the PIT timer before we start the SMP setup
	// After this we'll just use the APIC timer
	// disablePIC();
	pic_disable();

	// We need to init the LOCAL APIC for the BSP before we can touch anything else.
	// This abstracts away a LOT of writing to LAPIC registers
	bsp_init_lapic();

	// We're temporarily going to use the APIC timer in single shot mode during SMP setup.
	// I *really* don't want to deal with timer interrupts during this since the process is so fragile.
	// We can't really trust the TSC with super long delay's (unless it's invariant), it should be fine for this though.
	uint64_t tsc_freq = get_tsc_freq();
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "TSC FREQ: %zu\n", tsc_freq);
	printf_serial("[SMP] TSC Frequency calibrated: %zu Hz\r\n", tsc_freq);

	// Ideally we actually panic, this just gives me a better hint that something went wrong.
	if (!tsc_freq) {
		printf_serial("[SMP][BSP_INIT] FATAL: TSC Frequency is 0. Halting.\r\n");
		WALLOS_CLI_HLT();
	}

	uint64_t lapic_timer_freq = calibrate_lapic_timer_with_tsc(tsc_freq);
	printf_serial("[SMP] LAPIC Timer Freq: %zu\r\n", lapic_timer_freq);

	// We have two variables that need to be set for AP setup
	// We need the kernel stack, and we need the RAW pointer for the page tables to load into CR3.
	// We need to set the stack pointer every time we swap to a new AP.
	extern uint64_t pml4_pointer;
	extern uint64_t ap_stack_pointer;

	// We can just copy the current CR3
	uint64_t current_pml4;
	__asm__ volatile("mov %%cr3, %0" : "=r"(current_pml4));
	pml4_pointer = current_pml4;

	printf_serial("[SMP] APs will use PML4 at: %p\r\n", pml4_pointer);

	// This *should* be at 0x08000 (or in the same page).
	// As long as it's under 1MB, we're golden.
	extern void real_mode_trampoline_entry(void);

	// Need to set up APIC timer
	uint32_t bsp_apic_id = lapic_read(LAPIC_ID) >> 24;
	printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "BSP APIC ID: %d\n", bsp_apic_id);
	printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "MADT INFO:\n\tcount: %d\n", madt->entry_count);

	uint32_t expected_count = 1;
	bool reuse_stack = false;
	for (uint32_t i = 0; i < madt->entry_count; i++) {
		MADTEntry* e = &madt->entries[i];

		if (e->type != 0) continue;
		if (!(e->lapic.flags & 1)) continue;
		if (e->lapic.apic_id == bsp_apic_id) continue;

		uint32_t previous_count = ap_started_count;

		printf_serial("[SMP] Working on AP %d...\r\n", e->lapic.apic_id);

		uint8_t ap_apic_id = e->lapic.apic_id;

		// This is an AP
		// - APs start in real mode
		// 		Trampoline must:
		// 		- Disable interrupts
		// 		- Setup temporary GDT
		// 		- Enable protected mode
		// 		- Jump to 32-bit entry
		// 		- Enable paging
		// 		- Jump to 64-bit long mode
		// 		- Call ap_main()

		// 64KiB stack should be *way* more than needed.
		// We don't worry about tracking the allocation, it's never getting deallocated.
		// If something happens that takes an AP entirely offline, we have WAY bigger problems.

		if (!reuse_stack) {
			void* stack = kalloc(65536);
			ap_stack_pointer = (uint64_t) stack + 65536; // Stack grows backwards.
			printf_serial("[SMP] Allocated stack for AP %d at %p\r\n", ap_apic_id, ap_stack_pointer);
		} else {
			reuse_stack = false;
			printf_serial("[SMP] Last AP didn't startup correctly. Re-using stack for AP %d at %p\r\n", ap_apic_id, ap_stack_pointer);
		}

		// Send INIT-SIPI-SIPI
		// 		- Wait until ICR idle
		// 		- Send INIT IPI (Wait 10ms)
		// 		- Send SIPI (wait 200us)
		// 		- Send SIPI again.

		// Clear Errors
		lapic_write(LAPIC_ESR, 0);

		// Send INIT
		safe_printf_serial("[SMP] Sending INIT to AP %d\r\n", ap_apic_id);
		lapic_write(LAPIC_ICR_HIGH, (uint32_t) ap_apic_id << 24);
		lapic_write(LAPIC_ICR_LOW, 0x0000C500); // INIT, Assert, Level

		lapic_sleep_us(lapic_timer_freq, 10000); // 10ms

		// Send SIPI (Vector 0x08 for 0x8000)
		safe_printf_serial("[SMP] Sending SIPI 1 to AP %d\r\n", ap_apic_id);
		lapic_write(LAPIC_ICR_HIGH, (uint32_t) ap_apic_id << 24);
		lapic_write(LAPIC_ICR_LOW, 0x0000C608);

		lapic_sleep_us(lapic_timer_freq, 200); // 200us

		// Send second SIPI
		safe_printf_serial("[SMP] Sending SIPI 2 to AP %d\r\n", ap_apic_id);
		lapic_write(LAPIC_ICR_LOW, 0x0000C608);

		// We want to make sure it actually consumes the stack.
		// If it doesn't, we definitely have a problem.
		// We can also reuse the stack if it's not consumed (assuming we don't care if a single core is "bad")
		uint64_t wait_timeout = 100000;
		while (__atomic_load_n(&ap_stack_locked, __ATOMIC_SEQ_CST) == 1 && wait_timeout > 0) {
			__asm__ volatile("pause");
			wait_timeout--;
			goto end_loop;
		}

		// Wait for AP to signal ready
		uint64_t timeout = 5000;
		while (ap_started_count == previous_count && timeout > 0) {
			lapic_sleep_us(lapic_timer_freq, 1000);
			timeout--;
		}

	end_loop:

		if (ap_started_count > previous_count) {
			printf_color(PRINT_COLOR_GREEN, PRINT_DEFAULT_BG, "Processor %d: [OK]\n", ap_apic_id);
			expected_count++;
		} else {
			safe_printf_serial("[SMP] AP %d failed to increment counter.\r\n", ap_apic_id);
			printf_color(PRINT_COLOR_RED, PRINT_DEFAULT_BG, "Processor %d: [FAILED]\n", ap_apic_id);
		}
	}

	printf_serial("[SMP] Total Cores Online: %u\r\n", ap_started_count);
	printf("Total Cores Online: %u\n", ap_started_count);

	// We can now do the "final" setup for this.
	// We need to set up the IOAPIC to re-route interrupts
	// We need to set up actual timer interrupts
	// - PIT is currently set up for 1ms interrupts for system tick, I think we should just keep using it for system timekeeping.
	// - LAPIC should also be set up for 1ms timing for scheduling purposes.

	// Since we're currently still running terminal and stuff on BSP since we don't have a scheduler yet,
	// we should keep routing keyboard interrupts and serial interrupts to BSP. 
	// I need to "harden" a lot of the subsystems to have locks and atomic access interfaces.

	// WALLOS_CLI();
	ioapic_init(madt);

	extern bool pic_disabled;
	pic_disabled = true;

	// Route the PIT (ISA IRQ 0)
	ioapic_route_irq(0, 32, bsp_apic_id, false);
	printf_serial("[IOAPIC] PIT routed to vector 32 on BSP\r\n");

	// Route the Keyboard (ISA IRQ 1)
	i8042_flush(); // We need to flush the keyboard state and "re-enable" it
	ioapic_route_irq(1, 33, bsp_apic_id, false);
	printf_serial("[IOAPIC] Keyboard routed to vector 33 on BSP\r\n");

	// Route Serial COM1 (ISA IRQ 4)
	ioapic_route_irq(4, 36, bsp_apic_id, false);
	printf_serial("[IOAPIC] COM1 routed to vector 36 on BSP\r\n");

	// We need to set the LAPIC timer to 1ms here.

	// Finally, enable interrupts on the BSP
	WALLOS_STI();

	pit_init(1000);
}