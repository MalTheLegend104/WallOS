#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <scheduler/cpu.h>

#include <acpi/acpi_api.h>
#include <x86_64/lapic.h>
#include <system/idt.h>
#include <memory/virtual_mem.h>
#include <memory/kernel_alloc.h>

#include <x86_64/ioapic.h>
#include <x86_64/timing.h>
#include <system/timer.h>

void idle_task_main() {
	while (1) {
		__asm__ volatile("hlt");
	}
}

cpu_t system_cpus[WALLOS_SYSTEM_MAX_CPU];

cpu_t* cpu_current(void) { return NULL; }
cpu_t* cpu_get(uint32_t cpu_id) { (void) cpu_id; return NULL; }
uint32_t cpu_count(void) { return 0; }

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
	while (__atomic_test_and_set(&serial_print_lock, __ATOMIC_ACQUIRE));

	va_list arg;
	va_start(arg, format);
	vprintf_serial(format, arg);
	va_end(arg);

	__atomic_clear(&serial_print_lock, __ATOMIC_RELEASE);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Globals used by both BSP and APs
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// These are used on AP bringup
volatile uint32_t ap_started_count = 1; // BSP is already started
volatile uint32_t ap_stack_locked = 0;
volatile uint64_t bsp_tsc_freq;

// This is just debug information
volatile bool     lapic_timer_accuracy_mode = false;
volatile bool     cpu_online[WALLOS_SYSTEM_MAX_CPU];
volatile uint64_t lapic_freq_hz[WALLOS_SYSTEM_MAX_CPU];   // raw Hz
uint32_t          cpu_apic_ids[WALLOS_SYSTEM_MAX_CPU];    // apic_id at logical index i
uint32_t          cpu_online_count = 0;                   // final count after arch_init_cpus

// This is temporary, and a very shitty way of doing this.
uint64_t lapic_timer_ticks[WALLOS_SYSTEM_MAX_CPU];
volatile uint64_t lapic_ticks_per_ms[WALLOS_SYSTEM_MAX_CPU];

void ap_init_timer(uint8_t vector, uint64_t ticks_per_ms) {
	// Set the divisor. 0x3 = Divide by 16.
	// This makes the counter more manageable.
	lapic_write(LAPIC_DIVIDE_CONFIG, 0x3);

	// Set the LVT Timer Register
	// Bit 17:18 = 01 for Periodic Mode (0x20000)
	// Bits 0:7   = The interrupt vector
	lapic_write(LAPIC_LVT_TIMER, vector | 0x20000);

	// Set the Initial Count
	// As soon this is written, the timer starts counting down.
	lapic_write(LAPIC_INITIAL_COUNT, ticks_per_ms);

	lapic_write(LAPIC_LVT_TIMER, vector | (1 << 17)); // periodic
}

#include <system/idt.h>
WALLOS_INTERRUPT_HANDLER
void lapic_timer_int(struct interrupt_frame* frame) {
	(void) frame;
	uint32_t apic_id = lapic_read(0x20) >> 24;
	if (apic_id > WALLOS_SYSTEM_MAX_CPU) goto end;

	lapic_timer_ticks[apic_id] += 1;

	if (lapic_timer_accuracy_mode && lapic_timer_ticks[apic_id] % 1000 == 0)
		safe_printf_serial(
		"[AP%u] Timer: %llu ms (ticks/ms: %llu)\r\n",
		apic_id,
		lapic_timer_ticks[apic_id],
		lapic_ticks_per_ms[apic_id]
		);
end:
	lapic_write(LAPIC_EOI, 0);
}

#include <system/gdt.h>

void x86_ap_main() {
	// This signals to the BSP we *at least* consumed our stack.
	// This doesn't necessarily mean we're ready to go.
	// If we don't get to this point, the BSP will try to re-use the allocated stack space on the next AP.
	__atomic_store_n(&ap_stack_locked, 0, __ATOMIC_SEQ_CST);

	// Our base trampoline GDT is fine for bringup
	// It breaks once we try to start our new IDT
	// We also never set up a TSS for it.
	set_ap_gdt_and_tss();

	// This goes CLI -> LIDT -> STI
	// We immediately disable interrupts during lapic init. 
	ap_load_idt();
	WALLOS_CLI();

	ap_init_lapic();
	uint32_t apic_id = lapic_read(LAPIC_ID) >> 24;

	uint64_t local_lapic_freq = calibrate_lapic_timer_with_tsc(bsp_tsc_freq);

	lapic_freq_hz[apic_id] = local_lapic_freq;
	cpu_online[apic_id] = true;

	if (apic_id < WALLOS_SYSTEM_MAX_CPU) {
		lapic_ticks_per_ms[apic_id] = local_lapic_freq / 1000;
		safe_printf_serial("  [AP %d] LAPIC freq: %llu Hz (%llu ticks/ms)\n", apic_id, local_lapic_freq, lapic_ticks_per_ms[apic_id]);
	}

	safe_printf("  [AP %d] Hello\n", apic_id);
	// We're started enough to let the other APs init.
	__atomic_fetch_add(&ap_started_count, 1, __ATOMIC_SEQ_CST);

	WALLOS_STI();

	uint64_t tpm = (apic_id < WALLOS_SYSTEM_MAX_CPU) ? lapic_ticks_per_ms[apic_id] : lapic_ticks_per_ms[0];
	ap_init_timer(0xFD, tpm);

	// Halt while waiting for interrupt.
	// This should eventually probably go into a state waiting for an IPI, before we go to a scheduling loop.
	while (1) WALLOS_HLT();
}

/**
 * @brief This fully disables and remaps the PIC.
 * We need to reuse the vectors we set up earlier for PIT, KB, and Serial
 * This ensures we don't end up with any weirdness (which we shouldn't but QEMU is weird).
 */
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

WALLOS_INTERRUPT_HANDLER void lapic_spurious(struct interrupt_frame* f) {
	(void) f;
	lapic_write(LAPIC_EOI, 0);
}

void arch_init_cpus() {
	// We need to get the APIC from ACPI.

	MADTTable* madt = get_madt();
	uintptr_t virt_lapic_base = mapSequentialKernelPagesWithFlags(1, madt->lapic_base, BIT_SIZE | BIT_WRITE | BIT_PRESENT | BIT_PCD);
	printf_serial("[SMP] Found MADT at %p, identifying CPUs...\r\n", madt);

	set_lapic_base((uint64_t*) virt_lapic_base);

	enable_lapic_msr(madt->lapic_base);

	set_lapic_phys((uint64_t*) ((uintptr_t) madt->lapic_base));

	WALLOS_CLI(); // We disable these here otherwise we end up getting problems when we disable the PIC.
	// This entire process is a "bit" fragile, so we don't really want interrupts during it anyway.

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
	bsp_tsc_freq = tsc_freq;
	printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "TSC FREQ: %zu\n", tsc_freq);
	printf_serial("[SMP] TSC Frequency calibrated: %zu Hz\r\n", tsc_freq);

	// Ideally we actually panic, this just gives me a better hint that something went wrong.
	if (!tsc_freq) {
		printf_serial("[SMP][BSP_INIT] FATAL: TSC Frequency is 0. Halting.\r\n");
		WALLOS_CLI_HLT();
	}

	uint64_t lapic_timer_freq = calibrate_lapic_timer_with_tsc(tsc_freq);
	printf_serial("[SMP] LAPIC Timer Freq: %zu\r\n", lapic_timer_freq);

	uint32_t bsp_apic_id = lapic_read(LAPIC_ID) >> 24;
	lapic_freq_hz[bsp_apic_id] = lapic_timer_freq;
	lapic_ticks_per_ms[bsp_apic_id] = lapic_timer_freq / 1000;
	cpu_online[bsp_apic_id] = true;

	// uint64_t lapic_ticks_per_ms = lapic_timer_freq / 1000;

	// Add our LAPIC timer interrupt.
	// We reuse the IDT in the APs, so we need to do this before they all get started.
	add_interrupt_handler(0xFD, lapic_timer_int, 0, 0x8E);
	add_interrupt_handler(SPURIOUS_VECTOR, lapic_spurious, 0, 0x8E);

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

		__atomic_store_n(&ap_stack_locked, 1, __ATOMIC_SEQ_CST);

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
			// goto end_loop;
		}

		// Wait for AP to signal ready
		uint64_t timeout = 5000;
		while (ap_started_count == previous_count && timeout > 0) {
			lapic_sleep_us(lapic_timer_freq, 1000);
			timeout--;
		}

	// end_loop:

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
	// We need to flush the keyboard state and "re-enable" it
	// This is *very* hit or miss if it's needed on a particular system or not.
	i8042_flush();

	ioapic_route_irq(1, 33, bsp_apic_id, false);
	printf_serial("[IOAPIC] Keyboard routed to vector 33 on BSP\r\n");

	// Route Serial COM1 (ISA IRQ 4)
	ioapic_route_irq(4, 36, bsp_apic_id, false);
	printf_serial("[IOAPIC] COM1 routed to vector 36 on BSP\r\n");

	// We need to set the LAPIC timer to 1ms here.

	// Finally, enable interrupts on the BSP
	WALLOS_STI();

	pit_init(1000);

	cpu_online_count = ap_started_count;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// CPU Debug Command
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// This helper should probably be global...
void cpu_print(uint8_t color, const char* fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vprintf_serial(fmt, args);
	va_end(args);

	va_start(args, fmt);
	vprintf_color(color, PRINT_DEFAULT_BG, fmt, args);
	va_end(args);
}

void cpu_info_usage(void) {
	cpu_print(PRINT_COLOR_LIGHT_CYAN, "Usage: cpu_info <command> [args]\r\n");
	cpu_print(PRINT_COLOR_CYAN,
		"  init              Initialize all APs (debug use only)\r\n"
		"  list                List all online CPUs\r\n"
		"  freq                Show LAPIC timer frequency per CPU]\r\n"
		"  ticks               Show current timer tick counts per CPU\r\n"
		"  accuracy <on|off>   Enable/disable per-ms serial accuracy logging\r\n"
		"  status              Full system overview\r\n"
	);
}

void cpu_info_list(void) {
	uint32_t bsp_apic_id = lapic_read(LAPIC_ID) >> 24;
	cpu_print(PRINT_COLOR_LIGHT_GREEN, "[CPU] Online CPUs (%u total):\r\n", cpu_online_count);
	for (uint32_t i = 0; i < WALLOS_SYSTEM_MAX_CPU; i++) {
		if (!cpu_online[i]) continue;
		cpu_print(PRINT_COLOR_GREEN, "\t[APIC %u]%s\r\n", i, (i == bsp_apic_id) ? " (BSP)" : " (AP)");
	}
}

void cpu_info_freq(void) {
	cpu_print(PRINT_COLOR_PINK, "[CPU] LAPIC Timer Frequencies (divide-by-16):\r\n");
	for (uint32_t i = 0; i < WALLOS_SYSTEM_MAX_CPU; i++) {
		if (!cpu_online[i]) continue;
		uint64_t freq = lapic_freq_hz[i];
		cpu_print(PRINT_COLOR_PURPLE, "\t[APIC %u] %llu Hz  (~%llu MHz)  %llu ticks/ms\r\n", i, freq, freq / 1000000, lapic_ticks_per_ms[i]);
	}
}

void cpu_info_ticks(void) {
	cpu_print(PRINT_COLOR_YELLOW, "[CPU] Timer Ticks (1 tick = ~1ms):\r\n");
	for (uint32_t i = 0; i < WALLOS_SYSTEM_MAX_CPU; i++) {
		if (!cpu_online[i]) continue;
		uint64_t ticks = lapic_timer_ticks[i];
		cpu_print(PRINT_COLOR_BROWN, "\t[APIC %u] %llu ticks  (~%llu ms / ~%llu sec)\r\n", i, ticks, ticks, ticks / 1000);
	}
}

void cpu_info_status(void) {
	cpu_print(PRINT_COLOR_LIGHT_CYAN, "CPU System Status:\r\n");
	cpu_print(PRINT_COLOR_CYAN, "\tTotal online:      %u\r\n", cpu_online_count);
	cpu_print(PRINT_COLOR_CYAN, "\tBSP TSC Freq:      %llu Hz (~%llu MHz)\r\n", bsp_tsc_freq, bsp_tsc_freq / 1000000);
	cpu_print(PRINT_COLOR_CYAN, "\tAccuracy logging:  %s\r\n", lapic_timer_accuracy_mode ? "ON" : "OFF");
	cpu_print(PRINT_COLOR_CYAN, "\r\n");
	cpu_info_list();
	cpu_print(PRINT_COLOR_CYAN, "\r\n");
	cpu_info_freq();
	cpu_print(PRINT_COLOR_CYAN, "\r\n");
	cpu_info_ticks();
}

#include <terminal/terminal.h>
const ws_command_argument_t cpu_info_args[] = {
	{ WS_ARG_TYPE_GENERIC, false, "command", NULL, "One of: init, list, freq, ticks, status, accuracy." },
	{ WS_ARG_TYPE_GENERIC, false, "value",   NULL, "'on' or 'off' (accuracy only)." },
};
const size_t cpu_info_args_count = sizeof(cpu_info_args) / sizeof(cpu_info_args[0]);

int cpu_info(int argc, char** argv) {
	ws_context_t* ctx = ws_getCurrentContext();

	if (!ws_parse_args(ctx, argc, argv) || !ws_has_arg(ctx, "command")) {
		cpu_info_usage();
		return 1;
	}

	const char* cmd = ws_get_generic(ctx, "command");

	if (strcmp(cmd, "init") == 0) {
		arch_init_cpus();
	} else if (strcmp(cmd, "list") == 0) {
		cpu_info_list();
	} else if (strcmp(cmd, "freq") == 0) {
		cpu_info_freq();
	} else if (strcmp(cmd, "ticks") == 0) {
		cpu_info_ticks();
	} else if (strcmp(cmd, "status") == 0) {
		cpu_info_status();
	} else if (strcmp(cmd, "accuracy") == 0) {
		if (!ws_has_arg(ctx, "value")) {
			cpu_print(PRINT_COLOR_LIGHT_GREEN, "[CPU] Accuracy mode is %s. Usage: cpu_info accuracy <on|off>\r\n",
				lapic_timer_accuracy_mode ? "ON" : "OFF");
			return 1;
		}
		const char* value = ws_get_generic(ctx, "value");
		if (strcmp(value, "on") == 0) {
			lapic_timer_accuracy_mode = true;
			cpu_print(PRINT_COLOR_LIGHT_GREEN, "[CPU] Accuracy mode enabled.\r\n");
		} else if (strcmp(value, "off") == 0) {
			lapic_timer_accuracy_mode = false;
			cpu_print(PRINT_COLOR_LIGHT_GREEN, "[CPU] Accuracy mode disabled.\r\n");
		} else {
			cpu_print(PRINT_COLOR_LIGHT_RED, "[CPU] Unknown argument '%s'. Expected 'on' or 'off'.\r\n", value);
			return 1;
		}
	} else {
		cpu_print(PRINT_COLOR_LIGHT_RED, "[CPU] Unknown command '%s'.\r\n", cmd);
		cpu_info_usage();
		return 1;
	}

	return 0;
}