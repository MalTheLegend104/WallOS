#ifndef WALLOS_ACPI_API_H
#define WALLOS_ACPI_API_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif	

	// This is meant to provide an abstraction over uacpi and ACPICA to allow them to do the same basic things
	// We mostly just need them both for device discovery and shutdown/restart
	typedef enum {
		ACPI_DEVICE_UNKNOWN = -1,

		/* Core namespace structure */

		ACPI_DEVICE_SYSTEM_BUS = 1, // _SB_
		ACPI_DEVICE_PCI,            // PNP0A08 / PNP0A03 (PCI / PCIe root bridge)
		ACPI_DEVICE_PCI_ROOT_PORT,  // Downstream PCIe root ports (often PNP0A08 children)
		ACPI_DEVICE_PRT,            // _PRT (PCI Routing Table object, not a real device)

		/* Interrupts & timers */

		ACPI_DEVICE_PIT,            // PNP0000, legacy PIT
		ACPI_DEVICE_HPET,           // PNP0103, High Precision Event Timer
		ACPI_DEVICE_APIC,           // IOAPIC / LAPIC descriptor devices (MADT-backed)

		/* Memory & system resources */

		ACPI_DEVICE_SRD,            // PNP0C02, System Resource Device (reserved regions)
		ACPI_DEVICE_MB,             // PNP0C01, System Board / motherboard

		/* Power & system control */

		ACPI_DEVICE_POWERBUTTON,    // PNP0C0C
		ACPI_DEVICE_SLEEPBUTTON,    // PNP0C0E
		ACPI_DEVICE_LIDSWITCH,      // PNP0C0D (laptops)
		ACPI_DEVICE_BATTERY,        // PNP0C0A (laptops)
		ACPI_DEVICE_POWERSOURCE,    // ACPI0003 (AC vs battery)

		/*  Embedded / firmware glue */

		ACPI_DEVICE_EC,             // PNP0C09, Embedded Controller
		ACPI_DEVICE_IRQ_LINK,       // PNP0C0F, PCI Interrupt Link Device

		/* CPU & topology */

		ACPI_DEVICE_CPU,            // ACPI0007 / ACPI0010 processor objects
		ACPI_DEVICE_NUMA_NODE,      // NUMA proximity / SRAT-backed objects

		/* Input */

		ACPI_DEVICE_PS2_KEYBOARD,   // PNP0303
		ACPI_DEVICE_PS2_MOUSE,      // PNP0F13

		/* USB & hotplug */

		ACPI_DEVICE_USB_CONTROLLER, // PNP0D10 (abstract USB device)
		ACPI_DEVICE_PCIE_HOTPLUG,   // ACPI0016 (optional, defer handling)

		/* Graphics */

		ACPI_DEVICE_VIDEO,          // ACPI0004 (brightness / backlight)

		/* Virtual */

		ACPI_DEVICE_FIRMWARE,       // QEMU0002, VMBUS, or other firmware artifacts

	} wallos_acpi_dev_t; // The name is to try to avoid collisions, I have a feeling that apci_device is probably defined elsewhere

	typedef void* acpi_handle_t;

	typedef enum {
		ACPI_OK = 0,
		ACPI_ERR_UNSUPPORTED,
		ACPI_ERR_NOT_FOUND,
		ACPI_ERR_INVALID,
		ACPI_ERR_INTERNAL,
	} acpi_status_t;

	typedef enum {
		ACPI_SUBSYSTEM_NONE = 0,
		ACPI_SUBSYSTEM_ACPICA,
		ACPI_SUBSYSTEM_UACPI,
	} acpi_subsystem_t;

	acpi_status_t acpi_find_devices(wallos_acpi_dev_t type, acpi_handle_t* out_handles, size_t* count);

	/* Returns which subsystem is currently active.
	 * Should allow us to boot without any ACPI reliance.
	 */
	acpi_subsystem_t acpi_get_subsystem(void);

	/*

	 */

	/**
	 * @brief Early sanity check, kinda the same as acpi_get_subsystem().
	 * This explicitly checks if any ACPI tables even exist.
	 *
	 * @return true If the subsystem found any ACPI tables
	 * @return false If there is no ACPI subsystem or no tables were found.
	 */
	bool acpi_is_present(void);

	/* Power off the system (S5) */
	__attribute__((noreturn)) void acpi_shutdown(void);

	/* Reboot the system via ACPI reset register */
	__attribute__((noreturn)) void acpi_reboot(void);

	/* Enter a sleep state (S1-S4) */
	acpi_status_t acpi_sleep(uint8_t state);

	typedef struct {
		uint64_t base;
		uint64_t length;
		bool     mmio;
	} acpi_mem_resource_t;

	typedef struct {
		uint16_t gsi;
		bool     level_triggered;
		bool     active_low;
	} acpi_irq_resource_t;

	acpi_status_t acpi_get_resources(const acpi_handle_t handle, acpi_mem_resource_t* mem, size_t* mem_count, acpi_irq_resource_t* irq, size_t* irq_count);

	typedef struct {
		uint8_t  device;
		uint8_t  pin;   // INTA-INTD (0-3)
		uint32_t gsi;
	} acpi_pci_route_t;

	acpi_status_t acpi_get_pci_routing(const acpi_handle_t pci_root, acpi_pci_route_t* routes, size_t* route_count);


	typedef struct {
		uint32_t apic_id;
		bool     enabled;
	} acpi_cpu_info_t;

	acpi_status_t acpi_enumerate_cpus(acpi_cpu_info_t* cpus, size_t* cpu_count);

	void acpi_dump_namespace(void);
	void acpi_dump_tables(void);

	wallos_acpi_dev_t acpi_identify_handle(const acpi_handle_t handle);

#ifdef __cplusplus
}
#endif
#endif // WALLOS_ACPI_API_H