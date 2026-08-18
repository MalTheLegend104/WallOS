#include <drivers/driver_manager.h>
#include <device/device_manager.h>

#include <drivers/serial.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Internal global list of registered drivers
static wallos_driver_t* g_driver_list_head = NULL;

dm_error_t dm_register_driver(wallos_driver_t* driver) {
	if (!driver) return DM_ERROR_NULL_PARAM;

	// Add to the front of the linked list
	driver->next = g_driver_list_head;
	g_driver_list_head = driver;

	return DM_ERROR_NONE;
}

dm_error_t dm_remove_driver(wallos_driver_t* driver) {
	if (!driver) return DM_ERROR_NULL_PARAM;

	wallos_driver_t** curr = &g_driver_list_head;
	while (*curr) {
		if (*curr == driver) {
			*curr = driver->next;
			driver->next = NULL;
			return DM_ERROR_NONE;
		}
		curr = &((*curr)->next);
	}

	return DM_ERROR_NOSUCH_DRIVER;
}

dm_error_t dm_bind_device(struct wallos_device* dev) {
	if (!dev) return DM_ERROR_INVALID_DEV;
	if (dev->bound_driver) return DM_ERROR_ALREADY_BOUND;

	wallos_driver_t* driver = g_driver_list_head;

	while (driver) {
		// Bitmask check
		// Does the device provide the interfaces the driver requires?
		if ((dev->interfaces & driver->match_mask) == driver->match_flags) {

			// ID matching (if specified by driver)
			bool id_match = true;
			if (driver->vendor_id != 0 && driver->vendor_id != dev->vendor_id) id_match = false;
			if (driver->device_id != 0 && driver->device_id != dev->device_id) id_match = false;

			if (id_match) {
				// Optional Probe
				// Let the driver verify the hardware is actually compatible
				if (driver->ops.probe && driver->ops.probe(dev) == 0) {

					// Match found
					dev->bound_driver = driver;

					if (driver->ops.attach) {
						driver->ops.attach(dev);
					}

					return DM_ERROR_NONE;
				}
			}
		}
		driver = driver->next;
	}

	return DM_ERROR_DEV_NOT_FOUND; // No matching driver found
}

/**
 * @brief Cleanly detaches a driver from a device.
 */
dm_error_t dm_unbind_device(struct wallos_device* dev) {
	if (!dev) return DM_ERROR_INVALID_DEV;
	if (!dev->bound_driver) return DM_ERROR_NONE; // Already unbound

	if (dev->bound_driver->ops.detach) {
		dev->bound_driver->ops.detach(dev);
	}

	dev->bound_driver = NULL;
	// Note: The caller (or the driver) is responsible for cleaning up dev->driver_data during detach.

	return DM_ERROR_NONE;
}

#include <endian_bits.h>
void dm_bind_all_registered(void) {
	printf_serial("[DEVMGR] Starting global driver binding...\r\n");

	printf_color(PRINT_COLOR_GREEN, PRINT_DEFAULT_BG, "Starting global driver binding...\n");

	size_t bound_count = 0;
	size_t total_count = 0;

	for (device_node_t* node = internal_get_dev_registry(); node; node = node->next) {
		wallos_device_t* dev = node->dev;
		total_count++;

		if (DEV_INT_IS_ALREADY_BOUND(dev->interfaces)) {
			printf_serial("[DEVMGR] Device is already bound: %s\r\n", dev->name);
			bound_count++; // was bound during discovery/registry, doesn't go through this layer
			continue;
		}

		if (DEV_INT_IS_INTERFACE_ONLY(dev->interfaces)) {
			printf_serial("[DEVMGR] Deice is interface only: %s\r\n", dev->name);
			bound_count++; // all interfaces are explored by their driver. these are "bound"
			continue;
		}

		// Only attempt to bind if it's a "real" device and doesn't have a driver yet
		if (DEV_INT_IS_REAL(dev->interfaces) && dev->bound_driver == NULL) {
			dm_error_t err = dm_bind_device(dev);
			if (err == DM_ERROR_NONE) {
				bound_count++;
			}
		}
	}

	printf_serial("[DEVMGR] Binding complete. %d/%d devices bound to drivers.\r\n", bound_count, total_count);
	printf_color(PRINT_COLOR_GREEN, PRINT_DEFAULT_BG, "Binding complete. %d/%d devices bound to drivers.\r\n", bound_count, total_count);
}

int driver_cli(int argc, char** argv) {
	if (argc < 2) {
		printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "Driver commands:\n");
		printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG,
			"  driver list          - List all registered drivers\n"
			"  driver bind-all      - Attempt to bind all unbound devices\n"
			"  driver info <name>   - Show matching criteria for a driver\n"
		);
		return 0;
	}

	const char* cmd = argv[1];

	// list all drivers
	if (strcmp(cmd, "list") == 0) {
		printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "Registered Drivers:\n");
		wallos_driver_t* drv = g_driver_list_head;
		if (!drv) {
			printf("  No drivers registered.\n");
			return 0;
		}

		while (drv) {
			printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG, "  %-15s ", drv->name ? drv->name : "unnamed");
			printf_color(PRINT_COLOR_DARK_GREY, PRINT_DEFAULT_BG, "[Match: 0x%llx/0x%llx]\n",
				drv->match_flags, drv->match_mask);
			drv = drv->next;
		}
		return 0;
	}

	// bind all currently unbound devices
	if (strcmp(cmd, "bind-all") == 0) {
		printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "Starting global device binding...\n");
		dm_bind_all_registered();
		return 0;
	}

	// get info about a specific driver
	if (strcmp(cmd, "info") == 0) {
		if (argc < 3) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[ERROR] missing driver name\n");
			return -1;
		}

		wallos_driver_t* drv = g_driver_list_head;
		while (drv) {
			if (drv->name && strcmp(drv->name, argv[2]) == 0) {
				printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "Driver: %s\n", drv->name);
				printf("  Match Flags: 0x%llx\n", drv->match_flags);
				printf("  Match Mask:  0x%llx\n", drv->match_mask);
				if (drv->vendor_id != 0 || drv->device_id != 0) {
					printf("  Hardware ID: %04x:%04x\n", drv->vendor_id, drv->device_id);
				}

				// Scan registry to see what devices are currently bound to this driver
				printf("  Active Bindings:\n");
				// TODO: This doesnt actually do anything
				// I was too lazy to get the device registry and iterate through it again.
				return 0;
			}
			drv = drv->next;
		}
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[ERROR] driver '%s' not found\n", argv[2]);
		return -1;
	}

	printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[ERROR] unknown command '%s'\n", cmd);
	return -1;
}