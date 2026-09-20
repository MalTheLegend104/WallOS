#ifdef WALLOS_USE_UACPI
#include <uacpi/acpi.h>

#include <uacpi/uacpi.h>
#include <uacpi/uacpi.h>
#include <uacpi/tables.h>
#include <uacpi/types.h>
#include <uacpi/opregion.h>
#include <uacpi/notify.h>
#include <uacpi/event.h>
#include <uacpi/utilities.h>

// We probably aren't supposed to use this, but it provides super useful things to us
#include <uacpi/internal/tables.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include <panic.h>
#include <drivers/serial.h>
#include <klibc/logger.h>

#include <acpi/acpi_init.h>
#include <acpi/acpi_api.h>
#include <wall_shell.h>

static volatile bool power_button_pressed = false;
static volatile bool sleep_button_pressed = false;

/* Fixed hardware event handlers (run from SCI interrupt context) */

static uacpi_interrupt_ret uacpi_fixed_power_button_handler(uacpi_handle ctx) {
	power_button_pressed = true;
	printf("Power button handler invoked\n");
	return UACPI_INTERRUPT_HANDLED;
}

static uacpi_interrupt_ret uacpi_fixed_sleep_button_handler(uacpi_handle ctx) {
	sleep_button_pressed = true;
	return UACPI_INTERRUPT_HANDLED;
}

/* Control-method device notify handler (deferred work queue) */

static uacpi_status uacpi_system_notify_handler(uacpi_handle ctx, uacpi_namespace_node* node, uacpi_u64 value) {
	if (value != 0x80) { // 0x80 = Device-Specific generic notify
		return UACPI_STATUS_OK;
	}

	uacpi_object* hid_obj = NULL;
	uacpi_status status = uacpi_eval(node, "_HID", NULL, &hid_obj);

	if (status == UACPI_STATUS_OK && hid_obj != NULL) {
		if (hid_obj->type == UACPI_OBJECT_STRING) {
			if (!strcmp(hid_obj->buffer->text, "PNP0C0C")) {
				power_button_pressed = true;
				printf("Power button (control method) handler invoked\n");
			} else if (!strcmp(hid_obj->buffer->text, "PNP0C0E")) {
				sleep_button_pressed = true;
			}
		}
		uacpi_object_unref(hid_obj);
	}

	return UACPI_STATUS_OK;
}

void install_acpi_event_handlers(void) {
	uacpi_status status;

	// Power Button Fixed Event
	status = uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_POWER_BUTTON, uacpi_fixed_power_button_handler, NULL);
	if (uacpi_unlikely_error(status)) {
		printf("Failed to install power button handler: %s\n", uacpi_status_to_string(status));
	}

	// Sleep Button Fixed Event
	status = uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_SLEEP_BUTTON, uacpi_fixed_sleep_button_handler, NULL);
	if (uacpi_unlikely_error(status)) {
		printf("Failed to install sleep button handler: %s\n", uacpi_status_to_string(status));
	}

	// System Notify Handler
	status = uacpi_install_notify_handler(uacpi_namespace_root(), uacpi_system_notify_handler, NULL);
	if (uacpi_unlikely_error(status)) {
		printf("Failed to install ACPI system notify handler: %s\n", uacpi_status_to_string(status));
	}

	logger(INFO, "uACPI power/sleep button handlers installed.\n");
}

extern void acpi_process_deferred_work(void);

void acpi_poll_events(void) {
	acpi_process_deferred_work();

	if (power_button_pressed) {
		power_button_pressed = false;
		// logger(INFO, "ACPI power button event. Shutdown requested.\n");
		acpi_shutdown();
	}

	if (sleep_button_pressed) {
		sleep_button_pressed = false;
		logger(INFO, "ACPI sleep button event. Sleep requested...\n");
		// Sleep isn't implemented...
	}
}

extern bool ec_generic_handler(bool is_read, uint64_t address, uint32_t bit_width, uint64_t* value);

static uacpi_status uacpi_ec_handler(uacpi_region_op op, uacpi_handle op_data) {
	// uACPI sends attach/detach events when initializing or destroying region handlers
	if (op == UACPI_REGION_OP_ATTACH || op == UACPI_REGION_OP_DETACH) {
		return UACPI_STATUS_OK;
	}

	if (op != UACPI_REGION_OP_READ && op != UACPI_REGION_OP_WRITE) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	uacpi_region_rw_data* rw = (uacpi_region_rw_data*) op_data;
	bool is_read = (op == UACPI_REGION_OP_READ);
	uint32_t bit_width = rw->byte_width * 8;

	if (ec_generic_handler(is_read, rw->offset, bit_width, &rw->value)) {
		return UACPI_STATUS_OK;
	}

	return UACPI_STATUS_HARDWARE_TIMEOUT;
}

void acpi_install_ec_handler(void) {
	// Install globally. uACPI handles propagating this to the EC region.
	uacpi_status status = uacpi_install_address_space_handler(uacpi_namespace_root(), UACPI_ADDRESS_SPACE_EMBEDDED_CONTROLLER, uacpi_ec_handler, NULL);

	if (uacpi_unlikely_error(status)) {
		logger(ERROR, "Failed to install EC address space handler: %s\n", uacpi_status_to_string(status));
	}
}

void init_failure(const char* str) {
	const char* msg[] = { "uACPI initialization failed: ", str };
	printf("uACPI initialization failed: %s\n", str);
	printf_serial("uACPI initialization failed: %s\r\n", str);

	WALLOS_CLI_HLT();
	panic_sa(msg, 2);
}

void initialize_acpi(void) {
	printf_color(PRINT_COLOR_GREEN, PRINT_DEFAULT_BG, "Trying to initialize ACPI tables...\r\n");

	// uACPI's initialization phase handles table discovery and loading
	uacpi_status status = uacpi_initialize(0);
	if (uacpi_unlikely_error(status)) {
		printf_serial("Status: %s\r\n", uacpi_status_to_string(status));
		printf("uACPI Status: %s\r\n", uacpi_status_to_string(status));
		init_failure("Failed to initialize tables.");
	}

	printf_serial("Successfully loaded tables.\r\n");
	printf_color(PRINT_COLOR_GREEN, PRINT_DEFAULT_BG, "Successfully loaded tables.\n");

	logger(INFO, "uACPI Initialized.\n");

	// Load the AML namespace (equivalent to AcpiLoadTables)
	status = uacpi_namespace_load();
	if (uacpi_unlikely_error(status)) {
		init_failure("Failed to load namespace.");
	}

	logger(INFO, "uACPI loaded namespace.\n");

	// Initialize the namespace (calls _STA/_INI/_REG methods)
	status = uacpi_namespace_initialize();
	if (uacpi_unlikely_error(status)) {
		init_failure("Failed to initialize namespace.");
	}

	logger(INFO, "uACPI namespace initialized.\n");

	install_acpi_event_handlers();

	acpi_install_ec_handler();

	acpi_set_setup_completed();
}

#include <memory/kernel_alloc.h>

static char* uacpi_strdup(const char* str) {
	if (!str) return NULL;

	// ACPI names are exactly 4 characters and may not be null-terminated
	char* dup = kalloc(5);  // 4 chars + null terminator
	if (!dup) return NULL;

	// Copy up to 4 characters
	int i;
	for (i = 0; i < 4 && str[i] != '\0'; i++) {
		dup[i] = str[i];
	}
	dup[i] = '\0';

	return dup;
}

// Helper to build full path for a node
static char* get_full_path(uacpi_namespace_node* node) {
	uacpi_namespace_node_info* info;
	uacpi_status status = uacpi_get_namespace_node_info(node, &info);

	if (uacpi_unlikely_error(status)) {
		return NULL;
	}

	// Get the full path by walking up the tree
	static char path_buffer[256];
	path_buffer[0] = '\0';

	// Build path by traversing up - store segments in reverse
	char* segments[32];  // Max depth
	int segment_count = 0;

	uacpi_namespace_node* current = node;

	while (current != uacpi_namespace_root() && segment_count < 32) {
		uacpi_namespace_node_info* cur_info;
		status = uacpi_get_namespace_node_info(current, &cur_info);
		if (uacpi_unlikely_error(status)) break;

		// Duplicate the name so we can free cur_info
		segments[segment_count++] = uacpi_strdup(cur_info->name.text);

		// Now we can safely free the info
		uacpi_free_namespace_node_info(cur_info);

		current = uacpi_namespace_node_parent(current);
		if (!current) break;
	}

	// Build the path from root to node
	char* ptr = path_buffer;
	*ptr++ = '\\';  // Root

	// Add segments in reverse order
	for (int i = segment_count - 1; i >= 0; i--) {
		// Add dot separator if not first segment
		if (i < segment_count - 1) {
			*ptr++ = '.';
		}

		// Copy segment name
		char* seg = segments[i];
		while (*seg && ptr < path_buffer + 255) {
			*ptr++ = *seg++;
		}
	}

	*ptr = '\0';

	// Free all the duplicated strings
	for (int i = 0; i < segment_count; i++) {
		kfree(segments[i]);
	}

	uacpi_free_namespace_node_info(info);
	return path_buffer;
}

// Callback for namespace walking
static uacpi_iteration_decision walk_callback(void* user, uacpi_namespace_node* node, uacpi_u32 node_depth) {
	(void) user;
	(void) node_depth;

	uacpi_namespace_node_info* info;
	uacpi_status status = uacpi_get_namespace_node_info(node, &info);

	if (uacpi_unlikely_error(status)) {
		return UACPI_ITERATION_DECISION_CONTINUE;
	}

	// Only show device objects (type 6) to reduce noise, similar to ACPICA's ACPI_TYPE_DEVICE
	if (info->type == UACPI_OBJECT_DEVICE) {
		char* full_path = get_full_path(node);
		if (full_path) {
			printf("ACPI Object: %s\n", full_path);
			printf_serial("ACPI Object: %s\r\n", full_path);
		}
	}

	uacpi_free_namespace_node_info(info);

	return UACPI_ITERATION_DECISION_CONTINUE;
}

void walk_acpi_namespace(void) {
	uacpi_namespace_node* root = uacpi_namespace_root();

	// Use the correct function signature with all parameters
	uacpi_status status = uacpi_namespace_for_each_child(
		root,                               // parent node
		walk_callback,                      // descending callback
		NULL,                               // ascending callback (optional)
		0xFFFFFFFF,                         // type mask (all types)
		UACPI_MAX_DEPTH_ANY,                // max depth (all depths)
		NULL                                // user data
	);

	if (uacpi_unlikely_error(status)) {
		printf("Namespace walk failed: %s\n", uacpi_status_to_string(status));
	}
}

void print_acpi_device_info(const char* path) {
	uacpi_namespace_node* node;
	uacpi_status status = uacpi_namespace_node_find(NULL, path, &node);

	if (uacpi_unlikely_error(status)) {
		printf("ACPI path not found: %s\n", uacpi_status_to_string(status));
		return;
	}

	printf("Device: %s\n", path);

	// Get and print _HID
	uacpi_object* hid_obj = NULL;
	status = uacpi_eval(node, "_HID", NULL, &hid_obj);
	if (status == UACPI_STATUS_OK && hid_obj != NULL) {
		if (hid_obj->type == UACPI_OBJECT_STRING) {
			printf("  _HID: %s\n", hid_obj->buffer->text);
		} else if (hid_obj->type == UACPI_OBJECT_INTEGER) {
			printf("  _HID: 0x%llX\n", hid_obj->integer);
		}
		uacpi_object_unref(hid_obj);
	}

	// Get and print _CID
	uacpi_object* cid_obj = NULL;
	status = uacpi_eval(node, "_CID", NULL, &cid_obj);
	if (status == UACPI_STATUS_OK && cid_obj != NULL) {
		if (cid_obj->type == UACPI_OBJECT_STRING) {
			printf("  _CID: %s\n", cid_obj->buffer->text);
		} else if (cid_obj->type == UACPI_OBJECT_INTEGER) {
			printf("  _CID: 0x%llX\n", cid_obj->integer);
		}
		uacpi_object_unref(cid_obj);
	}

	// Get and print _ADR
	uacpi_u64 adr_value;
	status = uacpi_eval_simple_integer(node, "_ADR", &adr_value);
	if (status == UACPI_STATUS_OK) {
		printf("  _ADR: 0x%016llX\n", adr_value);
	}

	// Get and print _STA
	uacpi_u64 sta_value;
	status = uacpi_eval_simple_integer(node, "_STA", &sta_value);
	if (status == UACPI_STATUS_OK) {
		printf("  _STA: 0x%02llX (", sta_value);
		if (sta_value & 0x01) printf("Present ");
		if (sta_value & 0x02) printf("Enabled ");
		if (sta_value & 0x04) printf("ShowInUI ");
		if (sta_value & 0x08) printf("Functional ");
		if (sta_value & 0x10) printf("BatteryPresent ");
		printf(")\n");
	}

	// Get and print _CRS (Current Resource Settings)
	// Note: uACPI doesn't have a simple resource API like ACPICA
	// You would need to evaluate _CRS and parse the buffer manually
	uacpi_object* crs_obj = NULL;
	status = uacpi_eval(node, "_CRS", NULL, &crs_obj);
	if (status == UACPI_STATUS_OK && crs_obj != NULL) {
		if (crs_obj->type == UACPI_OBJECT_BUFFER) {
			printf("  _CRS: Resource Buffer Size: %llu bytes\n", crs_obj->buffer->size);
		}
		uacpi_object_unref(crs_obj);
	}
}

uacpi_iteration_decision uacpi_match_cb(void* user, struct uacpi_installed_table* tbl, uacpi_size idx) {
	struct acpi_sdt_hdr hdr = tbl->hdr;

	printf("Table %llu: %.4s | OEM ID: %.6s | OEM Table ID: %.8s\n",
		idx,
		hdr.signature,
		hdr.oemid,
		hdr.oem_table_id
	);

	printf_serial("Table %llu: %.4s | OEM ID: %.6s | OEM Table ID: %.8s\r\n",
		idx,
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
	printf("  Length: %u bytes\n", fadt->hdr.length);
	printf("  Revision: %u\n", fadt->hdr.revision);
	printf("  OEM ID: %.6s\n", fadt->hdr.oemid);
	printf("  OEM Table ID: %.8s\n", fadt->hdr.oem_table_id);
	printf("  SMI Command Port: 0x%X\n", fadt->smi_cmd);
	printf("  ACPI Enable: 0x%X, Disable: 0x%X\n", fadt->acpi_enable, fadt->acpi_disable);
	printf("  PM1a Event Block: 0x%X\n", fadt->pm1a_evt_blk);
	printf("  PM1a Control Block: 0x%X\n", fadt->pm1a_cnt_blk);
	printf("  SCI Interrupt: %u\n", fadt->sci_int);

	printf("Making assumption system is a: ");
	switch (fadt->preferred_pm_profile) {
		case 0: printf("Unspecified\n"); break;
		case 1: printf("Desktop\n"); break;
		case 2: printf("Mobile\n"); break;
		case 3: printf("Workstation\n"); break;
		case 4: printf("Enterprise Server\n"); break;
		case 5: printf("SOHO Server\n"); break;
		case 6: printf("Appliance PC\n"); break;
		case 7: printf("Performance Server\n"); break;
		default: printf("Reserved... (how did you get here?)\n"); break;
	}
}

static void print_hpet(void) {
	uacpi_table tbl;
	if (uacpi_table_find_by_signature("HPET", &tbl) != UACPI_STATUS_OK) {
		printf("HPET table not found\n");
		return;
	}

	struct acpi_hpet* hpet = (struct acpi_hpet*) tbl.hdr;

	printf("Signature:        %.4s\n", hpet->hdr.signature);
	printf("Length:           %u\n", hpet->hdr.length);
	printf("Revision:         %u\n", hpet->hdr.revision);
	printf("OEM ID:           %.6s\n", hpet->hdr.oemid);
	printf("OEM Table ID:     %.8s\n", hpet->hdr.oem_table_id);
	printf("OEM Revision:     0x%08X\n", hpet->hdr.oem_revision);
	printf("Creator ID:       %.4s\n", hpet->hdr.creator_id);
	printf("Creator Revision: 0x%08X\n", hpet->hdr.creator_revision);

	printf("\nHPET ID:          0x%08X\n", hpet->block_id);

	printf("Address:\n");
	printf("  Address Space:  0x%02X (%s)\n",
		hpet->address.address_space_id,
		hpet->address.address_space_id == 0 ? "System Memory" :
		hpet->address.address_space_id == 1 ? "System I/O" : "Other");
	printf("  Bit Width:      %u\n", hpet->address.register_bit_width);
	printf("  Bit Offset:     %u\n", hpet->address.register_bit_offset);
	printf("  Access Size:    %u\n", hpet->address.access_size);
	printf("  Address:        0x%016llX\n", hpet->address.address);

	printf("Sequence:         %u\n", hpet->number);
	printf("Minimum Tick:     %u femtoseconds\n", hpet->min_clock_tick);

	printf("Flags:            0x%02X\n", hpet->flags);
	printf("  Legacy IRQ Cap: %s\n", hpet->flags & 0x1 ? "Yes" : "No");

	uacpi_table_unref(&tbl);
}

static void print_table_info(const char* sig) {
	char S[5];
	for (int i = 0; i < 4; i++)
		S[i] = toupper(sig[i]);
	S[4] = '\0';

	uacpi_table tbl;
	if (uacpi_table_find_by_signature(S, &tbl) != UACPI_STATUS_OK) {
		printf("Failed to get ACPI table %.4s\n", S);
		return;
	}

	struct acpi_sdt_hdr* hdr = tbl.hdr;

	printf("ACPI Table: %.4s\n", hdr->signature);
	printf("  Length: %u bytes\n", hdr->length);
	printf("  Revision: %u\n", hdr->revision);
	printf("  OEM ID: %.6s\n", hdr->oemid);
	printf("  OEM Table ID: %.8s\n", hdr->oem_table_id);

	// Handle specific table types
	if (!memcmp(S, "FACP", 4) || !memcmp(S, "FADT", 4)) {
		print_fadt();
	} else if (!memcmp(S, "HPET", 4)) {
		print_hpet();
	} else if (!memcmp(S, "APIC", 4) || !memcmp(S, "MADT", 4)) {
		struct acpi_madt* madt = (struct acpi_madt*) tbl.hdr;
		printf("  Local APIC Address: 0x%X\n", madt->local_interrupt_controller_address);
		printf("  Flags: 0x%X (1=PCAT Dual 8259)\n", madt->flags);
		// TODO: Parse subtables if needed
	} else if (!memcmp(S, "MCFG", 4)) {
		// MCFG handling
		struct acpi_mcfg* mcfg = (struct acpi_mcfg*) tbl.hdr;
		uint8_t* ptr = (uint8_t*) mcfg + sizeof(struct acpi_mcfg);
		uint32_t count = (hdr->length - sizeof(struct acpi_mcfg)) / sizeof(struct acpi_mcfg_allocation);

		for (uint32_t i = 0; i < count; i++) {
			struct acpi_mcfg_allocation* alloc = (struct acpi_mcfg_allocation*) (ptr + i * sizeof(struct acpi_mcfg_allocation));
			printf("  - Base Address: 0x%llX (Segment %u, Busses %u-%u)\n",
				alloc->address, alloc->segment, alloc->start_bus, alloc->end_bus);
		}
	} else if (!memcmp(S, "DSDT", 4) || !memcmp(S, "SSDT", 4)) {
		printf("  - AML Bytecode Length: %u\n", hdr->length - sizeof(struct acpi_sdt_hdr));
	} else {
		printf("  - Table type not explicitly handled.\n");
	}

	uacpi_table_unref(&tbl);
}

// Referenced from kmain() when registering the "acpi" command.
const ws_command_argument_t acpi_args[] = {
	{ WS_ARG_TYPE_GENERIC, true,  "subcommand", NULL, "One of: hpet, fadt, list, walk, device, info." },
	{ WS_ARG_TYPE_GENERIC, false, "target",     NULL, "ACPI path (for 'device') or table signature (for 'info')." },
};
const size_t acpi_args_count = sizeof(acpi_args) / sizeof(acpi_args[0]);

int acpi_command(int argc, char** argv) {
	ws_context_t* ctx = ws_getCurrentContext();

	if (!ws_parse_args(ctx, argc, argv)) {
		return 0;
	}

	const char* subcommand = ws_get_generic(ctx, "subcommand");

	if (strcmp(subcommand, "hpet") == 0) {
		print_hpet();
	} else if (strcmp(subcommand, "fadt") == 0) {
		print_fadt();
	} else if (strcmp(subcommand, "list") == 0) {
		list_acpi_tables();
	} else if (strcmp(subcommand, "walk") == 0) {
		walk_acpi_namespace();
	} else if (strcmp(subcommand, "device") == 0 && ws_has_arg(ctx, "target")) {
		print_acpi_device_info(ws_get_generic(ctx, "target"));
	} else if (strcmp(subcommand, "info") == 0 && ws_has_arg(ctx, "target")) {
		print_table_info(ws_get_generic(ctx, "target"));
	} else {
		printf("Unknown or incomplete ACPI command.\n");
	}

	return 0;
}

#endif // WALLOS_USE_UACPI