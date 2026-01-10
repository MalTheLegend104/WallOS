#ifdef WALLOS_USE_ACPICA
#include <acpi.h>
#include <panic.h>
#include <stdio.h>
#include <drivers/serial.h>
#include <klibc/logger.h>
#include <acpi/acpi_init.h>

ACPI_STATUS acpi_device_callback(ACPI_HANDLE object, UINT32 nestingLevel, void* context, void** returnValue) {
	ACPI_BUFFER namebuf = { ACPI_ALLOCATE_BUFFER, NULL };
	if (ACPI_SUCCESS(AcpiGetName(object, ACPI_FULL_PATHNAME, &namebuf))) {
		printf("ACPI Object: %s\n", (char*) namebuf.Pointer);
		printf_serial("ACPI Object: %s\r\n", (char*) namebuf.Pointer);
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

void print_acpi_device_info(const char* path) {
	// ACPICA allows us to get a device by its string handle
	ACPI_HANDLE handle;
	ACPI_STATUS status = AcpiGetHandle(NULL, (char*) path, &handle);
	if (ACPI_FAILURE(status)) {
		printf("ACPI path not found: %s\n", AcpiFormatException(status));
		return;
	}

	printf("Device: %s\n", path);

	// Print _HID
	ACPI_OBJECT hidObj;
	ACPI_BUFFER hidBuf = { sizeof(ACPI_OBJECT), &hidObj };
	status = AcpiEvaluateObject(handle, "_HID", NULL, &hidBuf);
	if (ACPI_SUCCESS(status)) {
		if (hidObj.Type == ACPI_TYPE_STRING)
			printf("  _HID: %s\n", hidObj.String.Pointer);
		else if (hidObj.Type == ACPI_TYPE_INTEGER)
			printf("  _HID: 0x%llX\n", hidObj.Integer.Value);
	}

	// Print _CID
	ACPI_OBJECT cidObj;
	ACPI_BUFFER cidBuf = { sizeof(ACPI_OBJECT), &cidObj };
	status = AcpiEvaluateObject(handle, "_CID", NULL, &cidBuf);
	if (ACPI_SUCCESS(status)) {
		if (cidObj.Type == ACPI_TYPE_STRING)
			printf("  _CID: %s\n", cidObj.String.Pointer);
		else if (cidObj.Type == ACPI_TYPE_INTEGER)
			printf("  _CID: 0x%llX\n", cidObj.Integer.Value);
	}

	// Print _ADR
	ACPI_OBJECT adrObj;
	ACPI_BUFFER adrBuf = { sizeof(ACPI_OBJECT), &adrObj };
	status = AcpiEvaluateObject(handle, "_ADR", NULL, &adrBuf);
	if (ACPI_SUCCESS(status) && adrObj.Type == ACPI_TYPE_INTEGER) {
		printf("  _ADR: 0x%016llX\n", adrObj.Integer.Value);
	}

	// Print _STA
	ACPI_OBJECT staObj;
	ACPI_BUFFER staBuf = { sizeof(ACPI_OBJECT), &staObj };
	status = AcpiEvaluateObject(handle, "_STA", NULL, &staBuf);
	if (ACPI_SUCCESS(status) && staObj.Type == ACPI_TYPE_INTEGER) {
		UINT64 sta = staObj.Integer.Value;
		printf("  _STA: 0x%02llX (", sta);
		if (sta & 0x01) printf("Present ");
		if (sta & 0x02) printf("Enabled ");
		if (sta & 0x04) printf("ShowInUI ");
		if (sta & 0x08) printf("Functional ");
		if (sta & 0x10) printf("BatteryPresent ");
		printf(")\n");
	}

	// Print _CRS (not decoded, just length and raw buffer)
	ACPI_BUFFER crsBuf = { ACPI_ALLOCATE_BUFFER, NULL };
	status = AcpiGetCurrentResources(handle, &crsBuf);
	if (ACPI_SUCCESS(status)) {
		printf("  _CRS: Resource Buffer Size: %lu bytes\n", crsBuf.Length);
		AcpiOsFree(crsBuf.Pointer);
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
		printf("Creator ID:       %.4s\n", (char*) &hpet->Header.AslCompilerId);
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

#include <ctype.h>

void print_acpi_table_info(const char* sig) {
	ACPI_TABLE_HEADER* table;
	ACPI_STATUS status;
	char signature[5];
	strncpy(signature, sig, 4);
	signature[4] = '\0';

	for (int i = 0; i < 4; i++) {
		signature[i] = toupper(signature[i]);
	}

	// Handle RSDP separately
	if (!memcmp(signature, "RSDP", 4)) {
		ACPI_TABLE_RSDP* rsdp;
		status = AcpiGetTable((char*) "RSDP", 0, (ACPI_TABLE_HEADER**) &rsdp);
		if (ACPI_FAILURE(status)) {
			printf("Failed to get RSDP: %s\n", AcpiFormatException(status));
			return;
		}

		printf("RSDP:\n");
		printf("  Signature: %.8s\n", rsdp->Signature);
		printf("  OEM ID: %.6s\n", rsdp->OemId);
		printf("  Revision: %u\n", rsdp->Revision);
		printf("  RSDT Address: 0x%X\n", rsdp->RsdtPhysicalAddress);
		if (rsdp->Revision >= 2) {
			printf("  Length: %u\n", rsdp->Length);
			printf("  XSDT Address: 0x%llX\n", rsdp->XsdtPhysicalAddress);
		}
		return;
	}

	status = AcpiGetTable(signature, 0, &table);
	if (ACPI_FAILURE(status)) {
		printf("Failed to get ACPI table %.4s: %s\n", signature, AcpiFormatException(status));
		return;
	}

	printf("ACPI Table: %.4s\n", table->Signature);
	printf("  Length: %u bytes\n", table->Length);
	printf("  Revision: %u\n", table->Revision);
	printf("  OEM ID: %.6s\n", table->OemId);
	printf("  OEM Table ID: %.8s\n", table->OemTableId);

	if (!memcmp(table->Signature, "FACP", 4)) { // FADT
		ACPI_TABLE_FADT* fadt = (ACPI_TABLE_FADT*) table;
		printf("  SMI Command Port: 0x%X\n", fadt->SmiCommand);
		printf("  ACPI Enable: 0x%X, Disable: 0x%X\n", fadt->AcpiEnable, fadt->AcpiDisable);
		printf("  PM1a Event Block: 0x%X\n", fadt->Pm1aEventBlock);
		printf("  PM1a Control Block: 0x%X\n", fadt->Pm1aControlBlock);
		printf("  DSDT Address: 0x%X\n", fadt->Dsdt);
		if (fadt->Header.Length > 140) {
			printf("  X_DSDT Address: 0x%llX\n", fadt->XDsdt);
		}
		printf("Making assumption system is a: ");
		switch (fadt->PreferredProfile) {
			case 0: printf("Unspecified\n"); 		break;
			case 1: printf("Desktop\n"); 			break;
			case 2: printf("Mobile\n"); 			break;
			case 3: printf("Workstation\n"); 		break;
			case 4: printf("Enterprise Server\n"); 	break;
			case 5: printf("SOHO Server\n"); 		break;
			case 6: printf("Aplliance PC\n"); 		break;
			case 7: printf("Performance Server\n"); break;
			default: printf("Reserved... (how did you get here?)\n"); break;
		}
	} else if (!memcmp(table->Signature, "APIC", 4)) { // MADT
		ACPI_TABLE_MADT* madt = (ACPI_TABLE_MADT*) table;
		printf("  Local APIC Address: 0x%X\n", madt->Address);
		printf("  Flags: 0x%X (1=PCAT Dual 8259)\n", madt->Flags);

		// Subtable parsing
		ACPI_SUBTABLE_HEADER* sub = (ACPI_SUBTABLE_HEADER*) ((uint8_t*) madt + sizeof(ACPI_TABLE_MADT));
		while ((uint8_t*) sub < (uint8_t*) madt + madt->Header.Length) {
			if (sub->Type == ACPI_MADT_TYPE_LOCAL_APIC) {
				ACPI_MADT_LOCAL_APIC* la = (ACPI_MADT_LOCAL_APIC*) sub;
				printf("    - Processor Local APIC: ID %u, APIC ID %u, Flags 0x%X\n", la->ProcessorId, la->Id, la->LapicFlags);
			} else if (sub->Type == ACPI_MADT_TYPE_IO_APIC) {
				ACPI_MADT_IO_APIC* io = (ACPI_MADT_IO_APIC*) sub;
				printf("    - I/O APIC: ID %u, Address 0x%X, GSI Base %u\n", io->Id, io->Address, io->GlobalIrqBase);
			} else if (sub->Type == ACPI_MADT_TYPE_INTERRUPT_OVERRIDE) {
				ACPI_MADT_INTERRUPT_OVERRIDE* iso = (ACPI_MADT_INTERRUPT_OVERRIDE*) sub;
				printf("    - Int Source Override: Bus %u, IRQ %u -> GSI %u\n", iso->Bus, iso->SourceIrq, iso->GlobalIrq);
			}
			sub = (ACPI_SUBTABLE_HEADER*) ((uint8_t*) sub + sub->Length);
		}
	} else if (!memcmp(table->Signature, "MCFG", 4)) {
		// MCFG has a reserved 8-byte block before the base address allocations
		ACPI_MCFG_ALLOCATION* alloc = (ACPI_MCFG_ALLOCATION*) ((uint8_t*) table + sizeof(ACPI_TABLE_HEADER) + 8);
		uint32_t count = (table->Length - sizeof(ACPI_TABLE_HEADER) - 8) / sizeof(ACPI_MCFG_ALLOCATION);
		for (uint32_t i = 0; i < count; i++) {
			printf("  - Base Address: 0x%llX (Segment %u, Busses %u-%u)\n",
				alloc[i].Address, alloc[i].PciSegment, alloc[i].StartBusNumber, alloc[i].EndBusNumber);
		}
	} else if (!memcmp(table->Signature, "HPET", 4)) {
		print_hpet();
	} else if (!memcmp(table->Signature, "FADT", 4)) {
		print_fadt();
	} else if (!memcmp(table->Signature, "BGRT", 4)) {
		ACPI_TABLE_BGRT* bgrt = (ACPI_TABLE_BGRT*) table;
		printf("  - Boot Graphics: Type %u, Image at 0x%llX\n", bgrt->ImageType, bgrt->ImageAddress);
		printf("  - Image Offset: X=%u, Y=%u\n", bgrt->ImageOffsetX, bgrt->ImageOffsetY);
	} else if (!memcmp(table->Signature, "FPDT", 4)) {
		printf("  - Firmware Performance Data Table is present (Boot profiling).\n");
	} else if (!memcmp(table->Signature, "DSDT", 4) || !memcmp(table->Signature, "SSDT", 4)) {
		printf("  - AML Bytecode Length: %u\n", table->Length - sizeof(ACPI_TABLE_HEADER));
	} else {
		printf("  - Table type not explicitly handled.\n");
	}
}
void list_acpi_tables(void) {
	ACPI_TABLE_HEADER* table;
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

		printf_serial("Table %u: %.4s | OEM ID: %.6s | OEM Table ID: %.8s\r\n",
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
		else if (strcmp(argv[1], "device") == 0 && argc > 2)
			print_acpi_device_info(argv[2]);
		else if (strcmp(argv[1], "info") == 0 && argc > 2)
			print_acpi_table_info(argv[2]);
		else
			printf("Unknown or incomplete ACPI command.\n");
	} else {
		printf("Command requires arguments, I'm too lazy to add the help entry.");
	}

	return 0;
}

void init_failure(const char* str) {
	const char* msg[] = { "ACPICA initialization failed: ", str };
	printf("ACPICA initialization failed: %s\n", str);

	asm volatile("cli");
	asm volatile("hlt");
	panic_sa(msg, 2);
}

#define ACPI_MAX_INIT_TABLES 64
static ACPI_TABLE_DESC TableArray[ACPI_MAX_INIT_TABLES];

#include <drivers/serial.h>

void acpi_tables(void) {
	// printf_color(PRINT_COLOR_GREEN, PRINT_DEFAULT_BG, "Early ACPICA init...\r\n");


}

void initialize_acpi(void) {
	ACPI_STATUS status = AcpiInitializeSubsystem();
	if (ACPI_FAILURE(status)) {
		init_failure("Failed to initialize subsystem.");
	}

	logger(INFO, "ACPICA Initialized.\n");
	printf_color(PRINT_COLOR_GREEN, PRINT_DEFAULT_BG, "Trying to initalize ACPI tables...\r\n");

	status = AcpiInitializeTables(TableArray, ACPI_MAX_INIT_TABLES, FALSE);
	if (ACPI_FAILURE(status)) {
		printf_serial("Status: %d\r\n", status);
		printf("ACPICA Status: %d\r\n", status);
		init_failure("Failed to initialize tables.");
	}

	printf_serial("Successfully loaded tables.\r\n");
	printf_color(PRINT_COLOR_GREEN, PRINT_DEFAULT_BG, "Successfully loaded tables.\n");

	status = AcpiLoadTables();
	if (ACPI_FAILURE(status)) {
		init_failure("Failed to load tables.");
	}

	logger(INFO, "ACPICA loaded tables.\n");

	status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
	if (ACPI_FAILURE(status)) {
		init_failure("Failed to enable ACPI subsystem.");
	}


	logger(INFO, "ACPICA enabled subsystem.\n");

	status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
	if (ACPI_FAILURE(status)) {
		init_failure("Failed to initialize ACPI Objects.");
	}


	logger(INFO, "ACPICA driver fully initialized.\n");
}



#endif // WALLOS_USE_ACPICA