// Global pointers
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <acpi/acpi_api.h>
#include <memory/kernel_alloc.h>

#include <drivers/serial.h>

#ifdef WALLOS_USE_ACPICA
#include <acpi.h>
#elifdef WALLOS_USE_UACPI
#include <uacpi/acpi.h>
#include <uacpi/uacpi.h>
#include <uacpi/sleep.h>
// We probably aren't supposed to use this, but it provides super useful things to us
#include <uacpi/internal/tables.h>
#endif

MADTTable* parse_madt() {
	MADTTable* madt = (MADTTable*) kalloc(sizeof(MADTTable));
	if (!madt) return NULL;
	memset(madt, 0, sizeof(MADTTable));

#if defined(WALLOS_USE_ACPICA)
	ACPI_TABLE_MADT* acpi_madt;
	if (ACPI_FAILURE(AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**) &acpi_madt))) {
		kfree(madt);
		return NULL;
	}

	madt->lapic_base = acpi_madt->Address;
	madt->flags = acpi_madt->Flags;

	uint8_t* ptr = (uint8_t*) acpi_madt + sizeof(ACPI_TABLE_MADT);
	uint8_t* end = (uint8_t*) acpi_madt + acpi_madt->Header.Length;
	uint32_t table_length = acpi_madt->Header.Length;

#elif defined(WALLOS_USE_UACPI)
	struct uacpi_table tbl;
	if (uacpi_table_find_by_signature("APIC", &tbl) != UACPI_STATUS_OK) {
		kfree(madt);
		return NULL;
	}

	struct acpi_madt* uacpi_madt = (struct acpi_madt*) tbl.ptr;

	madt->lapic_base = uacpi_madt->local_interrupt_controller_address;
	madt->flags = uacpi_madt->flags;

	uint8_t* ptr = (uint8_t*) uacpi_madt->entries;  // entries[] starts right after the 44-byte header
	uint8_t* end = (uint8_t*) uacpi_madt + uacpi_madt->hdr.length;
	uint32_t table_length = uacpi_madt->hdr.length;
#endif

	printf_serial("[MADT] Table length: %u\r\n", table_length);
	printf_serial("[MADT] First byte at ptr: 0x%x, Second byte (len): 0x%x\r\n", ptr[0], ptr[1]);

	if (ptr >= end) {
		printf_serial("[MADT] No entries found in MADT\r\n");
		kfree(madt);
		return NULL;
	}

	if (ptr[1] == 0) {
		printf_serial("[MADT] FATAL: Entry length is zero at %p\r\n", ptr);
		kfree(madt);
		return NULL;
	}

	// Count entries
	uint32_t count = 0;
	uint8_t* tmp = ptr;
	while (tmp < end) {
		if (tmp[1] == 0) {
			printf_serial("[MADT] FATAL: Zero-length entry during count at %p\r\n", tmp);
			kfree(madt);
			return NULL;
		}
		count++;
		tmp += tmp[1];
	}

	madt->entry_count = count;
	madt->entries = (MADTEntry*) kalloc(sizeof(MADTEntry) * count);
	if (!madt->entries) {
		kfree(madt);
		return NULL;
	}
	memset(madt->entries, 0, sizeof(MADTEntry) * count);

	// Parse entries
	uint32_t idx = 0;
	while (ptr < end) {
		uint8_t type = ptr[0];
		uint8_t len = ptr[1];

		if (len == 0) {
			printf_serial("[MADT] FATAL: Zero-length entry during parse at %p\r\n", ptr);
			break;
		}

		MADTEntry* e = &madt->entries[idx++];
		e->type = type;
		e->length = len;

		switch (type) {
			case 0: // Processor Local APIC
				if (len >= 8) {
					e->lapic.acpi_processor_id = ptr[2];
					e->lapic.apic_id = ptr[3];
					e->lapic.flags = *(uint32_t*) (ptr + 4);
				}
				break;

			case 1: // IO APIC
				if (len >= 12) {
					e->ioapic.io_apic_id = ptr[2];
					e->ioapic.reserved = ptr[3];
					e->ioapic.io_apic_addr = *(uint32_t*) (ptr + 4);
					e->ioapic.gsi_base = *(uint32_t*) (ptr + 8);
				}
				break;

			default:
				printf_serial("[MADT] Skipping unknown entry type %u, len %u\r\n", type, len);
				break;
		}

		ptr += len;
	}

	return madt;
}
HPETTable* parse_hpet() {
	HPETTable* hpet = (HPETTable*) kalloc(sizeof(HPETTable));
	if (!hpet) return NULL;
	memset(hpet, 0, sizeof(HPETTable));

#if defined(WALLOS_USE_ACPICA)
	ACPI_TABLE_HPET* acpi_hpet;
	if (ACPI_FAILURE(AcpiGetTable(ACPI_SIG_HPET, 0, (ACPI_TABLE_HEADER**) &acpi_hpet))) {
		kfree(hpet);
		return NULL;
	}

	hpet->base_addr = acpi_hpet->Address.Address;
	hpet->hpet_number = acpi_hpet->Sequence;
	hpet->vendor_id = acpi_hpet->Id;
	hpet->min_tick = acpi_hpet->MinimumTick;
	hpet->flags = acpi_hpet->Flags & 1; // periodic flag

#elif defined(WALLOS_USE_UACPI)
	// struct uacpi_hpet* uacpi_hpet = (struct uacpi_hpet*) uacpi_find_table("HPET");
	// struct acpi_hpet* uacpi_hpet = (struct acpi_hpet*) uacpi_find_table("HPET");
	// if (!uacpi_hpet) {
	// 	kfree(hpet);
	// 	return NULL;
	// }

	struct uacpi_table tbl;
	if (uacpi_table_find_by_signature("HPET", &tbl) != UACPI_STATUS_OK) {
		kfree(hpet);
		return NULL;
	}

	struct acpi_hpet* uacpi_hpet = (struct acpi_hpet*) tbl.ptr;

	hpet->base_addr = uacpi_hpet->address.address_space_id;
	hpet->hpet_number = uacpi_hpet->block_id;
	hpet->vendor_id = uacpi_hpet->hdr.creator_id;
	hpet->min_tick = uacpi_hpet->min_clock_tick;
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