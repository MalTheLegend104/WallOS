#include <uacpi/acpi.h>

#include <acpi/acpi_init.h>

#include <uacpi/uacpi.h>
#include <uacpi/tables.h>
#include <uacpi/types.h>

// We probably aren't supposed to use this, but it provides super useful things to us
#include <uacpi/internal/tables.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#include <ctype.h>

#ifdef WALLOS_USE_UACPI

uint8_t uacpi_buf[4096];

void initialize_acpi(void) {
	uacpi_status st;

	st = uacpi_setup_early_table_access(uacpi_buf, 4096);
	if (st != UACPI_STATUS_OK) {
		printf("uACPI init failed: %s\n", uacpi_status_to_string(st));
		return;
	}
}

// void acpi_tables(void) {
// 	uacpi_table tbl;
// 	uacpi_status st;

// st = uacpi_table_find_by_signature("DSDT", &tbl);
// 	if (st != UACPI_STATUS_OK) {
// 		printf("No ACPI tables found\n");
// 		return;
// 	}

// 	for (;;) {
// 		struct acpi_sdt_hdr* hdr = tbl.hdr;

// 		printf("Table: %.4s | OEM ID: %.6s | OEM Table ID: %.8s\n",
// 			hdr->signature,
// 			hdr->oemid,
// 			hdr->oem_table_id
// 		);

// 		st = uacpi_table_find_next_with_same_signature(&tbl);
// 		if (st != UACPI_STATUS_OK)
// 			break;
// 	}

// 	uacpi_table_unref(&tbl);
// }

void acpi_tables() { }

uacpi_iteration_decision uacpi_match_cb(void* user, struct uacpi_installed_table* tbl, uacpi_size idx) {
	struct acpi_sdt_hdr hdr = tbl->hdr;

	printf("Table: %.4s | OEM ID: %.6s | OEM Table ID: %.8s\n",
		hdr.signature,
		hdr.oemid,
		hdr.oem_table_id
	);

	return UACPI_ITERATION_DECISION_CONTINUE;
}

void list_acpi_tables(void) {
	uacpi_for_each_table(0, uacpi_match_cb, NULL);

}

static void print_fadt(void) {
	struct acpi_fadt* fadt;
	if (uacpi_table_fadt(&fadt) != UACPI_STATUS_OK) {
		printf("FADT not available\n");
		return;
	}

	printf("FADT:\n");
	printf("  Preferred PM Profile: %u\n", fadt->preferred_pm_profile);
	printf("  SCI Interrupt: %u\n", fadt->sci_int);
	printf("  SMI Command Port: 0x%X\n", fadt->smi_cmd);
	printf("  ACPI Enable: 0x%X\n", fadt->acpi_enable);
	printf("  ACPI Disable: 0x%X\n", fadt->acpi_disable);
}

static void print_hpet(void) {
	uacpi_table tbl;
	if (uacpi_table_find_by_signature("HPET", &tbl) != UACPI_STATUS_OK) {
		printf("HPET table not found\n");
		return;
	}

	struct acpi_hpet* hpet = (struct acpi_hpet*) tbl.hdr;

	printf("HPET:\n");
	printf("  Address: 0x%llX\n", hpet->address.address);
	printf("  Sequence: %u\n", hpet->number);
	printf("  Minimum Tick: %u fs\n", hpet->min_clock_tick);
	printf("  Legacy IRQ Capable: %s\n",
		(hpet->flags & 1) ? "Yes" : "No");

	uacpi_table_unref(&tbl);
}

// static uacpi_iteration_decision parse_madt(uacpi_handle _, struct acpi_entry_hdr* hdr) {
// 	switch (hdr->type) {
// 		case ACPI_MADT_TYPE_LOCAL_APIC: {
// 				struct acpi_madt_local_apic* la =
// 					(struct acpi_madt_local_apic*) hdr;
// 				printf("  LAPIC: CPU %u, APIC ID %u, Flags 0x%X\n",
// 					la->processor_id, la->apic_id, la->flags);
// 				break;
// 			}
// 		case ACPI_MADT_TYPE_IO_APIC: {
// 				struct acpi_madt_io_apic* io =
// 					(struct acpi_madt_io_apic*) hdr;
// 				printf("  IOAPIC: ID %u, Addr 0x%X, GSI %u\n",
// 					io->io_apic_id, io->address, io->global_irq_base);
// 				break;
// 			}
// 	}

// 	return UACPI_ITERATION_DECISION_CONTINUE;
// }

// static void print_madt(void) {
// 	uacpi_table tbl;
// 	if (uacpi_table_find_by_signature("APIC", &tbl) != UACPI_STATUS_OK) {
// 		printf("MADT not found\n");
// 		return;
// 	}

// 	uacpi_for_each_subtable(
// 		tbl.hdr,
// 		sizeof(struct acpi_madt),
// 		parse_madt,
// 		NULL
// 	);

// 	uacpi_table_unref(&tbl);
// }

static void print_table_info(const char* sig) {
	char S[4];
	for (int i = 0; i < 4; i++)
		S[i] = toupper(sig[i]);

	uacpi_table tbl;
	if (uacpi_table_find_by_signature(S, &tbl) != UACPI_STATUS_OK) {
		printf("ACPI table %.4s not found\n", S);
		return;
	}

	struct acpi_sdt_hdr* hdr = tbl.hdr;

	printf("ACPI Table %.4s\n", hdr->signature);
	printf("  Length: %u\n", hdr->length);
	printf("  Revision: %u\n", hdr->revision);
	printf("  OEM ID: %.6s\n", hdr->oemid);
	printf("  OEM Table ID: %.8s\n", hdr->oem_table_id);

	uacpi_table_unref(&tbl);
}

int acpi_command(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: acpi <list|info|fadt|hpet|madt>\n");
		return 0;
	}

	if (!strcmp(argv[1], "list"))
		list_acpi_tables();
	else if (!strcmp(argv[1], "info") && argc > 2)
		print_table_info(argv[2]);
	else if (!strcmp(argv[1], "fadt"))
		print_fadt();
	else if (!strcmp(argv[1], "hpet"))
		print_hpet();
	// else if (!strcmp(argv[1], "madt"))
	// 	print_madt();
	else
		printf("Unknown ACPI command\n");

	return 0;
}

#endif // WALLOS_USE_UACPI