#include <acpi/acpi_api.h>
#include <stdio.h>
#include <cpu_io.h>
#include <arch.h>

#ifdef WALLOS_USE_ACPICA
#include <acpi.h>
#elifdef WALLOS_USE_UACPI
#include <uacpi/acpi.h>
#include <uacpi/uacpi.h>
#include <uacpi/sleep.h>
#endif

acpi_status_t acpi_find_devices(wallos_acpi_dev_t type, acpi_handle_t* out_handles, size_t* count);

acpi_subsystem_t acpi_get_subsystem(void) {
#ifdef WALLOS_USE_ACPICA
	return ACPI_SUBSYSTEM_ACPICA;
#elifdef WALLOS_USE_UACPI
	return ACPI_SUBSYSTEM_UACPI;
#endif
	return ACPI_SUBSYSTEM_NONE;
}

#include <klibc/multiboot.h>

bool acpi_is_present(void) {
	if (getAcpiRoot() != NULL) return 0;
	return false;
}

bool is_acpi_setup_complete = false;
bool acpi_setup_complete(void) { return is_acpi_setup_complete; }
void acpi_set_setup_completed(void) { is_acpi_setup_complete = true; }

__attribute__((noreturn)) void acpi_shutdown(void) {
	printf("Initiating ACPI shutdown...\n");

#ifdef WALLOS_USE_ACPICA
	ACPI_STATUS status;

	// Enter sleep state S5 (soft off)
	status = AcpiEnterSleepStatePrep(5);
	if (ACPI_FAILURE(status)) {
		printf("Failed to prepare for sleep state S5: %s\n", AcpiFormatException(status));
		goto shutdown_failed;
	}

	// Disable interrupts before final shutdown
	cpu_disable_interrupts();

	status = AcpiEnterSleepState(5);
	if (ACPI_FAILURE(status)) {
		printf("Failed to enter sleep state S5: %s\n", AcpiFormatException(status));
		goto shutdown_failed;
	}

#elifdef WALLOS_USE_UACPI

		// Enter sleep state S5 (soft off)
	uacpi_status status = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
	if (uacpi_unlikely_error(status)) {
		printf("Failed to prepare for sleep state S5: %s\n", uacpi_status_to_string(status));
		goto shutdown_failed;
	}

	// Disable interrupts before final shutdown
	cpu_disable_interrupts();

	status = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
	if (uacpi_unlikely_error(status)) {
		printf("Failed to enter sleep state S5: %s\n", uacpi_status_to_string(status));
		goto shutdown_failed;
	}

#endif

shutdown_failed:
	// If we get here, shutdown failed
	printf("ACPI shutdown failed, system halted. It's safe to force shutdown the computer.\n");
	// TODO: REVISIT THIS AFTER SMP HAS BEEN FINISHED
	// ALL CPUS MUST HANG PERMANENTLY
	WALLOS_HANG();
	__builtin_unreachable();
}

__attribute__((noreturn)) void acpi_reboot(void) {
	printf("Initiating ACPI reboot...\n");
#ifdef WALLOS_USE_ACPICA
	ACPI_STATUS status;


	// Try the ACPI reset register first (most reliable on modern systems)
	status = AcpiReset();
	if (ACPI_SUCCESS(status)) {
		// Wait a bit for reset to take effect
		for (int i = 0; i < 1000000; i++) {
			WALLOS_PAUSE();
		}
	}
#elifdef WALLOS_USE_UACPI
	uacpi_status status = uacpi_reboot();
	if (status == UACPI_STATUS_OK) {
		// Wait a bit for reset to take effect
		for (int i = 0; i < 1000000; i++) {
			WALLOS_PAUSE();
		}
}
#endif

	// If ACPI reset didn't work, try a few legacy methods
	printf("ACPI reset failed, trying keyboard controller reset...\n");

	// Method 1: Keyboard controller reset (8042)
	uint8_t good = 0x02;
	while (good & 0x02) {
		good = inb(0x64);
	}
	outb(0x64, 0xFE); // Reset command

	// Wait a bit
	for (int i = 0; i < 1000000; i++) {
		WALLOS_PAUSE();
	}

	// Method 2: Triple fault (last resort)
	printf("Keyboard controller reset failed, triggering triple fault...\n");
	cpu_disable_interrupts();

	// Load invalid IDT to cause triple fault
	struct {
		uint16_t limit;
		uint64_t base;
	} __attribute__((packed)) invalid_idt = { 0, 0 };

	__asm__ volatile("lidt %0" : : "m"(invalid_idt));
	__asm__ volatile("int $0x00"); // Trigger interrupt with invalid IDT

	// Should never reach here
	printf("Failed to restart, somehow...\nIt's safe to force shutdown the computer.\n");
	WALLOS_HANG();
	__builtin_unreachable();
}

acpi_status_t acpi_sleep(uint8_t state);

acpi_status_t acpi_get_resources(const acpi_handle_t handle, acpi_mem_resource_t* mem, size_t* mem_count, acpi_irq_resource_t* irq, size_t* irq_count);

acpi_status_t acpi_get_pci_routing(const acpi_handle_t pci_root, acpi_pci_route_t* routes, size_t* route_count);

void acpi_dump_namespace(void);
void acpi_dump_tables(void);

wallos_acpi_dev_t acpi_identify_handle(const acpi_handle_t handle);
