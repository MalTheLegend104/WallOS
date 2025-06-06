#include <acpi.h>
#include <panic.h>
#include <stdio.h>
#include <drivers/serial.h>
#include <klibc/logger.h>
#include <acpi/acpi_init.h>

ACPI_STATUS acpi_device_callback(ACPI_HANDLE object, UINT32 nestingLevel,
								 void *context, void **returnValue) {
	ACPI_BUFFER namebuf = { ACPI_ALLOCATE_BUFFER, NULL };

	if (ACPI_SUCCESS(AcpiGetName(object, ACPI_FULL_PATHNAME, &namebuf))) {
		printf("ACPI Object: %s\n", (char *)namebuf.Pointer);
		printf_serial("ACPI Object: %s\r\n", (char *)namebuf.Pointer);
		AcpiOsFree(namebuf.Pointer);
	}

	return AE_OK;
}

void walk_acpi_namespace(void) {
	ACPI_STATUS status = AcpiWalkNamespace(ACPI_TYPE_DEVICE,
										   ACPI_ROOT_OBJECT,
										   UINT32_MAX,
										   acpi_device_callback,
										   NULL, NULL, NULL);
	if (ACPI_FAILURE(status)) {
		printf("Namespace walk failed: %s\n", AcpiFormatException(status));
	}
}

void print_hpet() {
	ACPI_TABLE_HEADER* table;
	const ACPI_STATUS status = AcpiGetTable(ACPI_SIG_HPET, 1, &table);
	if (ACPI_FAILURE(status)) {
		printf("HPET table was not found.\n");
	} else {
		ACPI_TABLE_HPET* hpet = (ACPI_TABLE_HPET*) table;
		printf("Signature:        %.4s\n", hpet->Header.Signature);
		printf("Length:           %u\n", hpet->Header.Length);
		printf("Revision:         %u\n", hpet->Header.Revision);
		printf("OEM ID:           %.6s\n", hpet->Header.OemId);
		printf("OEM Table ID:     %.8s\n", hpet->Header.OemTableId);
		printf("OEM Revision:     0x%08X\n", hpet->Header.OemRevision);
		printf("Creator ID:       %.4s\n", (char *)&hpet->Header.AslCompilerId);
		printf("Creator Revision: 0x%08X\n", hpet->Header.AslCompilerRevision);

		// Print HPET-specific info
		printf("\nHPET ID:          0x%08X\n", hpet->Id);
		printf("  Vendor ID:      0x%04X\n", hpet->Id & 0xFFFF);
		printf("  Device ID:      0x%04X\n", (hpet->Id >> 16) & 0xFFFF);

		printf("Address:\n");
		printf("  Address Space:  0x%02X (%s)\n",
			hpet->Address.SpaceId,
			hpet->Address.SpaceId == 0 ? "System Memory" :
			hpet->Address.SpaceId == 1 ? "System I/O" : "Other");
		printf("  Bit Width:      %u\n", hpet->Address.BitWidth);
		printf("  Bit Offset:     %u\n", hpet->Address.BitOffset);
		printf("  Access Size:    %u\n", hpet->Address.AccessWidth);
		printf("  Address:        0x%016llX\n", hpet->Address.Address);

		printf("Sequence:         %u\n", hpet->Sequence);
		printf("Minimum Tick:     %u femtoseconds\n", hpet->MinimumTick);

		printf("Flags:            0x%02X\n", hpet->Flags);
		printf("  Legacy IRQ Cap: %s\n", hpet->Flags & 0x1 ? "Yes" : "No");
	}
}

void print_fadt() {
	ACPI_TABLE_HEADER* table;
	const ACPI_STATUS status = AcpiGetTable(ACPI_SIG_FADT, 1, &table);
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
}

void list_acpi_tables(void) {
	ACPI_TABLE_HEADER *table;
	UINT32 index = 0;

	while (1) {
		const ACPI_STATUS status = AcpiGetTableByIndex(index, &table);
		if (ACPI_FAILURE(status)) {
			if (status == AE_LIMIT) { break; }

			printf("Final index %u: %s\n", index, AcpiFormatException(status));
			break;
		}

		printf("Table %u: %.4s | OEM ID: %.6s | OEM Table ID: %.8s\n",
			   index,
			   table->Signature,
			   table->OemId,
			   table->OemTableId);

		index++;
	}
}

int acpi_command(int argc, char** argv) {
	if (argc > 1) {
		if (strcmp(argv[1], "hpet") == 0)
			print_hpet();
		else if (strcmp(argv[1], "fadt") == 0)
			print_fadt();
		else if (strcmp(argv[1], "list") == 0)
			list_acpi_tables();
		else if (strcmp(argv[1], "walk") == 0)
			walk_acpi_namespace();
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
	const ACPI_STATUS status = AcpiInitializeTables(TableArray, ACPI_MAX_INIT_TABLES, FALSE);
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
