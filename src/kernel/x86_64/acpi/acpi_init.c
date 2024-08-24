#include <acpi.h>
#include <panic.h>
#include <stdio.h>
#include <klibc/logger.h>
#include <acpi/acpi_init.h>

#pragma GCC diagnostic ignored "-Wunused-parameter" 
int acpi_command(int argc, char** argv) {
	// acpi_tag* acpi = MultibootManager::getACPI();
	// RSDP_t* r = acpi->rsdp;
	// puts_vga_color("ACPI INFO:\n", VGA_COLOR_PINK, VGA_DEFAULT_BG);
	// set_colors(VGA_COLOR_PURPLE, VGA_DEFAULT_BG);
	// printf("\tSignature: ");
	// The signature is not null terminated, but is guaranteed to be 8 characters long
	// for (int i = 0; i < 8; i++) {
	// 	putc_vga(r->signature[i]);
	// }
	// printf("\n\tOEM: %s\n", r->OEMID);
	// printf("\tAddress: 0x%x\n", r->rsdtAddress);
	// set_to_last();

	// Test FADT header.
	ACPI_TABLE_HEADER* table;
	ACPI_STATUS status = AcpiGetTable(ACPI_SIG_FADT, 1, &table);

	if (ACPI_FAILURE(status)) {
		// Handle error
	} else {
		// Parse the FADT table
		ACPI_TABLE_FADT* fadt = (ACPI_TABLE_FADT*) table;
		printf("FADT pointer addr: 0x%llx\n", fadt);
		printf("Making assumption system is a: ");
		switch (fadt->PreferredProfile) {
			case 1: {
					printf("Unspecified.");
					break;
				}
			case 2: {
					printf("Desktop.");
					break;
				}
			case 3: {
					printf("Mobile.");
					break;
				}
			case 4: {
					printf("Workstation.");
					break;
				}
			case 5: {
					printf("Enterprise Server.");
					break;
				}
			case 6: {
					printf("SOHO Server.");
					break;
				}
			case 7: {
					printf("Aplliance PC.");
					break;
				}
			default: {
					printf("Performance Server.");
					break;
				}
		}
		printf("\n");
	}
	return 0;
}

void init_failure(const char* str) {
	const char* msg[] = { "ACPICA initialization failed: ", str };
	panic_sa(msg, 2);
}

#define ACPI_MAX_INIT_TABLES 16
static ACPI_TABLE_DESC TableArray[ACPI_MAX_INIT_TABLES];

#include <drivers/serial.h>

void acpi_tables(void) {
	ACPI_STATUS status;
	status = AcpiInitializeTables(TableArray, ACPI_MAX_INIT_TABLES, FALSE);
	if (ACPI_FAILURE(status)) {
		printf_serial("Status: %d\r\n", status);
		init_failure("Failed to initialize tables.");
	}

	set_colors(VGA_COLOR_GREEN, VGA_DEFAULT_BG);
	printf_serial("Successfully loaded tables.\r\n");
	set_to_last();
}

void initialize_acpi(void) {
	ACPI_STATUS status = AcpiInitializeSubsystem();
	if (ACPI_FAILURE(status)) {
		init_failure("Failed to initialize subsystem.");
	}

	logger(INFO, "ACPICA Initialized.\n");

	status = AcpiLoadTables();
	if (ACPI_FAILURE(status)) {
		init_failure("Failed to load tables.");
	}

	logger(INFO, "ACPICA loaded tables.\n");

	// Test example header.
	ACPI_TABLE_HEADER* table;
	status = AcpiGetTable(ACPI_SIG_FADT, 1, &table);

	if (ACPI_FAILURE(status)) {
		// Handle error
	} else {
		// Parse the FADT table
		ACPI_TABLE_FADT* fadt = (ACPI_TABLE_FADT*) table;
		printf("FADT pointer addr: 0x%llx\n", fadt);
	}
}
