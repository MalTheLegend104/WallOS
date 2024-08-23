#include <acpi.h>
#include <panic.h>
#include <stdio.h>
#include <klibc/logger.h>
#include <acpi/acpi_init.h>

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

	logger(INFO, "Actually loaded tables.");
	printf_serial("Successfully loaded tables.\r\n");

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
