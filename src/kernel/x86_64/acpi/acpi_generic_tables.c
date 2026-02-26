// Global pointers
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <acpi/acpi_api.h>


#ifdef WALLOS_USE_ACPICA
#include <acpi.h>
#elifdef WALLOS_USE_UACPI
#include <uacpi/acpi.h>
#include <uacpi/uacpi.h>
#include <uacpi/sleep.h>
#endif

MADTTable* parse_madt() {
	MADTTable* madt = (MADTTable*) malloc(sizeof(MADTTable));
	if (!madt) return NULL;
	memset(madt, 0, sizeof(MADTTable));

#if defined(WALLOS_USE_ACPICA)
	ACPI_TABLE_MADT* acpi_madt;
	if (ACPI_FAILURE(AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**) &acpi_madt))) {
		free(madt);
		return NULL;
	}

	madt->lapic_base = acpi_madt->Address;
	uint8_t* ptr = (uint8_t*) acpi_madt + sizeof(ACPI_TABLE_MADT);
	uint8_t* end = (uint8_t*) acpi_madt + acpi_madt->Header.Length;

#elif defined(WALLOS_USE_UACPI)
	struct uacpi_table* uacpi_madt = uacpi_find_table("APIC");
	if (!uacpi_madt) {
		free(madt);
		return NULL;
	}

	madt->lapic_base = uacpi_madt->lapic_address;
	uint8_t* ptr = (uint8_t*) uacpi_madt + sizeof(struct uacpi_table_header);
	uint8_t* end = (uint8_t*) uacpi_madt + uacpi_madt->length;

#endif

	// Count entries
	uint32_t count = 0;
	uint8_t* tmp = ptr;
	while (tmp < end) {
		count++;
		tmp += tmp[1]; // advance by entry length
	}

	madt->entry_count = count;
	madt->entries = (MADTEntry*) malloc(sizeof(MADTEntry) * count);
	if (!madt->entries) {
		free(madt);
		return NULL;
	}

	// Parse entries
	uint32_t idx = 0;
	while (ptr < end) {
		uint8_t type = ptr[0];
		uint8_t len = ptr[1];

		MADTEntry* e = &madt->entries[idx++];
		e->type = type;
		e->length = len;

		switch (type) {
			case 0: // Processor Local APIC
				e->lapic.acpi_processor_id = ptr[2];
				e->lapic.apic_id = ptr[3];
				e->lapic.flags = *(uint32_t*) (ptr + 4);
				break;
			case 1: // IO APIC
				e->ioapic.io_apic_id = ptr[2];
				e->ioapic.reserved = ptr[3];
				e->ioapic.io_apic_addr = *(uint32_t*) (ptr + 4);
				e->ioapic.gsi_base = *(uint32_t*) (ptr + 8);
				break;
			default:
				// ignore other types
				break;
		}

		ptr += len;
	}

	return madt;
}

HPETTable* parse_hpet() {
	HPETTable* hpet = (HPETTable*) malloc(sizeof(HPETTable));
	if (!hpet) return NULL;
	memset(hpet, 0, sizeof(HPETTable));

#if defined(WALLOS_USE_ACPICA)
	ACPI_TABLE_HPET* acpi_hpet;
	if (ACPI_FAILURE(AcpiGetTable(ACPI_SIG_HPET, 0, (ACPI_TABLE_HEADER**) &acpi_hpet))) {
		free(hpet);
		return NULL;
	}

	hpet->base_addr = acpi_hpet->Address.Address;
	hpet->hpet_number = acpi_hpet->Sequence;
	hpet->vendor_id = acpi_hpet->Id;
	hpet->min_tick = acpi_hpet->MinimumTick;
	hpet->flags = acpi_hpet->Flags & 1; // periodic flag

#elif defined(WALLOS_USE_UACPI)
	struct uacpi_table* uacpi_hpet = uacpi_find_table("HPET");
	if (!uacpi_hpet) {
		free(hpet);
		return NULL;
	}

	hpet->base_addr = uacpi_hpet->hpet_address;
	hpet->hpet_number = uacpi_hpet->sequence;
	hpet->vendor_id = uacpi_hpet->vendor_id;
	hpet->min_tick = uacpi_hpet->min_tick;
	hpet->flags = uacpi_hpet->flags & 1;

#endif

	hpet->main_counter = 0; // not read yet
	return hpet;
}

static MADTTable* g_madt = NULL;
static HPETTable* g_hpet = NULL;

MADTTable* get_madt() {
	if (!g_madt)
		g_madt = parse_madt();
	return g_madt;
}

HPETTable* get_hpet() {
	if (!g_hpet)
		g_hpet = parse_hpet();
	return g_hpet;
}