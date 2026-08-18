#include <system/timer.h>
#include <x86_64/timing.h>
#include <acpi/acpi_api.h>
#include <drivers/serial.h>
#include <memory/virtual_mem.h>
#include <memory/kernel_alloc.h>

#include <stdio.h>
#include <endian_bits.h>

#define HPET_REG_CAPABILITIES 0x000
#define HPET_REG_CONFIG       0x010
#define HPET_REG_MAIN_COUNTER 0x0F0

#define HPET_CAP_COUNT_SIZE_BIT BIT(13) // 1 = 64-bit main counter, 0 = 32-bit
#define HPET_CFG_ENABLE_BIT     BIT(0)  // overall HPET enable

// Standard reserved size of the HPET MMIO block according the spec
#define HPET_MMIO_SIZE 0x400

static counter_clock_t hpet_counter;
static volatile uint64_t* hpet_regs;
static bool hpet_is_64bit;

static uint64_t hpet_counter_read(counter_clock_t* self) {
	(void) self;
	uint64_t value = hpet_regs[HPET_REG_MAIN_COUNTER / 8];
	if (!hpet_is_64bit) value &= 0xFFFFFFFFULL; // top 32 bits undefined on 32-bit counters
	return value;
}

bool hpet_init(void) {
	HPETTable* table = get_hpet();
	if (!table) {
		printf_serial("[HPET] no HPET table present, skipping...\r\n");
		return false;
	}
	if (table->base_addr == 0) {
		printf_serial("[HPET] HPET table present but base_addr is 0, skipping...\r\n");
		return false;
	}

	// printf_serial("[HPET] found HPET #%d at phys 0x%llx\r\n", table->hpet_number, (unsigned long long) table->base_addr);

	uintptr_t virt = mapKernelLocationWithFlags((uintptr_t) table->base_addr, HPET_MMIO_SIZE, PDE_FLAGS_UC_2MB);
	hpet_regs = (volatile uint64_t*) virt;
	// printf_serial("[HPET] mapped MMIO block to virt 0x%llx\r\n", (unsigned long long) virt);

	uint64_t capabilities = hpet_regs[HPET_REG_CAPABILITIES / 8];
	// femtoseconds per tick
	// technically the ACPI table has this info, but I've seen it be very inconsistent and sometimes zero, so we want to get the actual value
	uint64_t period_fs = capabilities >> 32;
	hpet_is_64bit = (capabilities & HPET_CAP_COUNT_SIZE_BIT) != 0;

	// printf_serial("[HPET] capabilities=0x%llx period=%lluf s counter_width=%d-bit\r\n", (unsigned long long) capabilities, (unsigned long long) period_fs, hpet_is_64bit ? 64 : 32);

	if (period_fs == 0) {
		printf_serial("[hpet] period reported as 0 (bad BIOS?), refusing to register...\r\n");
		return false; // misreporting BIOS, we don't want to divide by zero
	}

	// Enable the main counter
	// We intention leave the LEG_RT_CNF untouched, we just let the PIT be used for system timing.
	hpet_regs[HPET_REG_CONFIG / 8] |= HPET_CFG_ENABLE_BIT;

	uint64_t frequency_hz = 1000000000000000ULL / period_fs; // 1e15 fs/s / period

	hpet_counter = (counter_clock_t){
		.name = "hpet",
		.rating = 400, // Basically the only better timer available to us is a TSC/APIC timer 
		.frequency_hz = frequency_hz,
		.counter_bits = hpet_is_64bit ? 64 : 32,
		.read = hpet_counter_read,
	};
	counter_clock_register(&hpet_counter);

	// By the time we can register HPET, we already need virtual memory, so we should have access to kalloc
	wallos_device_t* dev = (wallos_device_t*) kcalloc(1, sizeof(wallos_device_t));
	dev->interfaces = DEV_INT_TIMER | DEV_INT_ALREADY_BOUND;
	dev->parent = get_root_timer();
	dev->name = "HPET";
	register_device(dev);
	// this timer should live as long as the system does, we don't worry about cleanup

	printf_serial("[HPET] Enabled, registered as counter_clock at %llu Hz\r\n", (uint64_t) frequency_hz);
	printf_color(PRINT_COLOR_PINK, PRINT_DEFAULT_BG, "[HPET] Enabled HPET at %llu Hz", (uint64_t) frequency_hz);

	return true;
}