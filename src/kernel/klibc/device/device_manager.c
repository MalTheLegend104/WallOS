#include <memory/kernel_alloc.h>
#include <device/device_manager.h>
#include <string.h>
#include <stdio.h>
#include <drivers/serial.h>

#include <stdbool.h>

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Internal device registry
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Simple singly-linked list of all registered devices.
// The device manager owns this list; subsystems hold pointers into it.

static device_node_t* device_registry = NULL;

device_node_t* internal_get_dev_registry() {
	return device_registry;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

char* get_device_path(wallos_device_t* dev) {
	if (!dev) return NULL;

	const char* dev_prefix = "/dev";

	// Walk up to root, counting how much space we need
	size_t total_len = strlen(dev_prefix); // Initial length is the length of "/dev"
	wallos_device_t* cursor = dev;
	while (cursor) {
		const char* seg = cursor->name ? cursor->name : "unknown";
		total_len += 1 + strlen(seg); // '/' + name
		cursor = cursor->parent;
	}

	char* path = (char*) kalloc(total_len + 1);
	if (!path) return NULL;

	// Build the string right-to-left, leaving room for the /dev prefix
	path[total_len] = '\0';
	size_t pos = total_len;
	cursor = dev;
	while (cursor) {
		const char* seg = cursor->name ? cursor->name : "unknown";
		size_t seg_len = strlen(seg);

		pos -= seg_len;
		memcpy(path + pos, seg, seg_len);
		pos -= 1;
		path[pos] = '/';

		cursor = cursor->parent;
	}

	// pos should now be exactly strlen(dev_prefix), write the prefix at the front
	memcpy(path, dev_prefix, strlen(dev_prefix));

	return path; // Caller must kfree()
}


wallos_device_t* find_device_by_path(const char* path) {
	for (device_node_t* node = device_registry; node; node = node->next) {
		wallos_device_t* dev = node->dev;
		if (dev->path && strcmp(dev->path, path) == 0) {
			return dev;
		}
	}
	return NULL;
}

wallos_device_t* find_device_by_name(const char* name) {
	if (!name) return NULL;

	for (device_node_t* node = device_registry; node; node = node->next) {
		if (node->dev && node->dev->name && strcmp(node->dev->name, name) == 0) {
			return node->dev;
		}
	}
	return NULL;
}

wallos_device_t* resolve_device(const char* input) {
	if (!input) return NULL;

	// Treat anything starting with '/' as a path
	if (input[0] == '/') {
		return find_device_by_path(input);
	}

	// Otherwise treat as name
	return find_device_by_name(input);
}

static bool device_is_unbound(wallos_device_t* dev) {
	if (!dev) return false;
	if (DEV_INT_IS_ALREADY_BOUND(dev->interfaces)) return false;
	if (DEV_INT_IS_INTERFACE_ONLY(dev->interfaces)) return false;
	return dev->bound_driver == NULL;
}


static bool device_is_bound(wallos_device_t* dev) {
	if (!dev) return false;
	if (DEV_INT_IS_INTERFACE_ONLY(dev->interfaces)) return false;
	if (DEV_INT_IS_ALREADY_BOUND(dev->interfaces)) return true;
	return dev->bound_driver != NULL;
}


typedef enum {
	DEV_LIST_FILTER_ALL,
	DEV_LIST_FILTER_UNBOUND,
	DEV_LIST_FILTER_BOUND
} dev_list_filter_t;

static bool device_matches_filter(wallos_device_t* dev, dev_list_filter_t filter) {
	switch (filter) {
		case DEV_LIST_FILTER_UNBOUND: return device_is_unbound(dev);
		case DEV_LIST_FILTER_BOUND:   return device_is_bound(dev);
		case DEV_LIST_FILTER_ALL:
		default:                      return true;
	}
}


static void print_device_flags(uint64_t flags) {
	if (flags == DEV_INT_NONE) {
		printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "  Flags: none\n");
		return;
	}

	printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "  Flags: 0x%llx\n", flags);
	printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG, "  Decoded:\n");

	for (int i = 0; i < 64; i++) {
		uint64_t bit = (1ULL << i);
		if (!(flags & bit)) continue;

		const char* name = get_flag_name((device_interface_t) bit);

		if (name) {
			printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "    - %s\n", name);
		} else {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "    - unknown_bit_%d\n", i);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Registry Functions
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

wallos_device_t* create_device(device_interface_flags_t flags, const char* name) {
	if (DEV_INT_IS_INVALID(flags)) return NULL;

	wallos_device_t* dev = (wallos_device_t*) kalloc(sizeof(wallos_device_t));
	if (!dev) return NULL;

	// This is the easiest way to make sure everything is set to NULL
	memset(dev, 0, sizeof(wallos_device_t));

	dev->interfaces = flags;
	dev->name = name; // Caller owns this string — we just hold the pointer.
					  // If a caller needs a dynamic name they must ensure its lifetime.

	return dev;
}

void remove_device(wallos_device_t* dev) {
	if (!dev) return;

	// Unlink from parent's child list
	if (dev->parent) {
		wallos_device_t** cursor = &dev->parent->first_child;
		while (*cursor && *cursor != dev) {
			cursor = &(*cursor)->next_sibling;
		}
		if (*cursor) {
			*cursor = dev->next_sibling;
		}
	}

	// Unlink from the registry
	device_node_t** cursor = &device_registry;
	while (*cursor && (*cursor)->dev != dev) {
		cursor = &(*cursor)->next;
	}
	if (*cursor) {
		device_node_t* dead = *cursor;
		*cursor = dead->next;
		kfree(dead);
	}

	if (dev->path) {
		kfree(dev->path);
		dev->path = NULL;
	}

	// We do NOT free dev->driver_data here. The bound driver/subsystem
	// owns that allocation and must clean it up before calling remove_device.
	// We also do NOT recursively remove children. The caller is responsible
	// for tearing down the subtree in the right order (leaves first).
	// The caller is also responsible for cleaning up the name*
	kfree(dev);
}


void register_device(wallos_device_t* dev) {
	if (!dev) return;

	if (dev->path) {
		kfree(dev->path);
	}
	dev->path = get_device_path(dev);

	device_node_t* node = (device_node_t*) kalloc(sizeof(device_node_t));
	if (!node) {
		printf_serial("[DEVMGR] GP Avoided: Registry allocation failed\n");
		return;
	}

	node->dev = dev;
	node->next = device_registry;
	device_registry = node;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Device Path 
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Internal helper to update the path of a device and all its descendants
void update_device_path_recursive(wallos_device_t* dev) {
	if (!dev) return;

	// Free the old path if it exists
	if (dev->path) {
		kfree(dev->path);
		dev->path = NULL;
	}

	dev->path = get_device_path(dev);

	// Recursively update all children because their paths just changed too!
	wallos_device_t* child = dev->first_child;
	while (child) {
		update_device_path_recursive(child);
		child = child->next_sibling;
	}
}

// Public function for drivers to request a path refresh
void recalculate_device_path(wallos_device_t* dev) {
	if (!dev) return;
	update_device_path_recursive(dev);
}

static void print_device_list_entry(wallos_device_t* dev) {
	const char* display_path = dev->path ? dev->path : "<no path>";
	printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "%s ", display_path);
	printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "(0x%llx)\n", (uint64_t) dev->interfaces);
	printf_serial("[DEVMGR] %s vid:did=%04x:%04x dev=%p\r\n", display_path, dev->vendor_id, dev->device_id, dev);
}

void print_device_list_recursive(wallos_device_t* dev, dev_list_filter_t filter) {
	DEV_FOR_EACH_CHILD(dev, child) {
		if (device_matches_filter(child, filter)) {
			print_device_list_entry(child);
		}
		print_device_list_recursive(child, filter);
	}
}



int get_device_color(device_interface_t interfaces) {
	if (interfaces == DEV_INT_INVALID)       return PRINT_COLOR_RED;
	if (interfaces & DEV_INT_UNKNOWN)        return PRINT_COLOR_YELLOW;
	if (interfaces & DEV_INT_INTERFACE_ONLY) return PRINT_COLOR_LIGHT_GREEN;
	return PRINT_COLOR_WHITE;
}

void print_device_tree_recursive(wallos_device_t* dev, int depth, const char* prefix, bool is_last, int max_depth) {
	const char* name = dev->name ? dev->name : "unknown";
	int name_color = get_device_color(dev->interfaces);

	if (depth == 0) {
		// Root node. Just print the name, no connector
		printf_color(name_color, PRINT_DEFAULT_BG, "%s\n", name);
		printf_serial("%s\n", name);
	} else {
		// Print the inherited prefix from parent levels, then this node's connector
		printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "%s", prefix);
		printf_serial("%s", prefix);

		const char* connector = is_last ? "\xc0\xc4\xc4" : "\xc3\xc4\xc4";  // └── or ├──
		printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "%s", connector);
		printf_serial("%s", connector);

		printf_color(name_color, PRINT_DEFAULT_BG, "%s ", name);
		printf_serial("%s ", name);

		printf_color(PRINT_COLOR_DARK_GREY, PRINT_DEFAULT_BG, "(0x%llx)\n", (uint64_t) dev->interfaces);
		printf_serial("(0x%llx)\r\n", (uint64_t) dev->interfaces);
	}

	if (!DEV_HAS_CHILDREN(dev)) return;

	// Build the prefix for children:
	// If this node is the last sibling, children get "   " (no continuation bar).
	// Otherwise they get "\xb3  " (│ + two spaces) to draw the continuing vertical.
	size_t prefix_len = strlen(prefix);
	char* child_prefix = (char*) kalloc(prefix_len + 4); // 3 chars + null
	if (!child_prefix) return;

	memcpy(child_prefix, prefix, prefix_len);

	if (depth == 0) {
		// Root's children start fresh
		child_prefix[prefix_len + 0] = ' ';
		child_prefix[prefix_len + 1] = ' ';
		child_prefix[prefix_len + 2] = ' ';
	} else {
		child_prefix[prefix_len + 0] = is_last ? ' ' : '\xb3'; // ' ' or │
		child_prefix[prefix_len + 1] = ' ';
		child_prefix[prefix_len + 2] = ' ';
	}
	child_prefix[prefix_len + 3] = '\0';

	if (max_depth != -1 && depth >= max_depth) {
		kfree(child_prefix);
		return;
	}

	// Walk children — we need to know which one is last, so peek ahead
	wallos_device_t* child = dev->first_child;
	while (child) {
		bool child_is_last = (child->next_sibling == NULL);
		print_device_tree_recursive(child, depth + 1, child_prefix, child_is_last, max_depth);
		child = child->next_sibling;
	}
	kfree(child_prefix);
}

void print_device_tree(wallos_device_t* dev, int max_depth) {
	if (dev) {
		print_device_tree_recursive(dev, 0, "", false, max_depth);
		return;
	}
	for (device_node_t* node = device_registry; node; node = node->next) {
		if (!node->dev->parent) {
			print_device_tree_recursive(node->dev, 0, "", false, max_depth);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Device Command Function
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

void print_dev_brief(wallos_device_t* dev) {
	const char* name = dev->name ? dev->name : "unknown";

	printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "[%04x:%04x] ", dev->vendor_id, dev->device_id);
	printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG, "%s ", name);
	printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "(0x%llx)\n", (uint64_t) dev->interfaces);

	printf_serial("[DEVMGR] dev=%p name='%s' vid:did=%04x:%04x flags=0x%llx parent=%p first_child=%p next_sibling=%p\r\n",
		dev,
		name,
		dev->vendor_id, dev->device_id,
		(uint64_t) dev->interfaces,
		dev->parent,
		dev->first_child,
		dev->next_sibling
	);
}

int device_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "Device commands:\n");
		printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG,
			"  dev list [name]        - List registered devices (paths)\n"
			"  dev tree [name] [-d n] - Display the full device hierarchy\n"
			"  dev path <name>        - Get the path of a specific device\n"
			"  dev info <name>        - Show identity and topology info\n"
			"  dev refresh <name>     - Recalculate path for a device\n"
		);

		printf_serial("[DEVMGR] usage requested\r\n");
		return 0;
	}

	const char* cmd = argv[1];

	// ------------------------------------------------------------------------
	// dev list
	// ------------------------------------------------------------------------
	if (strcmp(cmd, "list") == 0) {
		wallos_device_t* root = NULL;
		dev_list_filter_t filter = DEV_LIST_FILTER_ALL;

		for (int i = 2; i < argc; i++) {
			if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--unbound") == 0) {
				if (filter == DEV_LIST_FILTER_BOUND) {
					printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[error] --unbound and --bound are mutually exclusive\n");
					return -1;
				}
				filter = DEV_LIST_FILTER_UNBOUND;
			} else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bound") == 0) {
				if (filter == DEV_LIST_FILTER_UNBOUND) {
					printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[error] --unbound and --bound are mutually exclusive\n");
					return -1;
				}
				filter = DEV_LIST_FILTER_BOUND;
			} else {
				root = resolve_device(argv[i]);
				if (!root) {
					printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[error] device '%s' not found\n", argv[i]);
					return -1;
				}
			}
		}

		const char* header = "Registered devices:\n";
		if (filter == DEV_LIST_FILTER_UNBOUND) header = "Unbound devices:\n";
		else if (filter == DEV_LIST_FILTER_BOUND) header = "Bound devices:\n";
		printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, header);

		if (root) {
			// Print the root itself (if it matches the filter), then all descendants
			if (device_matches_filter(root, filter)) {
				print_device_list_entry(root);
			}
			print_device_list_recursive(root, filter);
		} else {
			for (device_node_t* node = device_registry; node; node = node->next) {
				wallos_device_t* dev = node->dev;
				if (device_matches_filter(dev, filter)) {
					print_device_list_entry(dev);
				}
			}
		}
		return 0;
	}



	// ------------------------------------------------------------------------
	// dev tree
	// ------------------------------------------------------------------------
	if (strcmp(cmd, "tree") == 0) {
		printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "Device tree:\n");

		wallos_device_t* root = NULL;
		int max_depth = -1; // -1 = unlimited

		for (int i = 2; i < argc; i++) {
			if ((strcmp(argv[i], "--depth") == 0 || strcmp(argv[i], "-d") == 0) && i + 1 < argc) {
				max_depth = (int) strtol(argv[++i], NULL, 10);
			} else {
				root = resolve_device(argv[i]);
				if (!root) {
					printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[error] device '%s' not found\n", argv[i]);
					return -1;
				}
			}
		}

		print_device_tree(root, max_depth);
		printf_serial("[DEVMGR] tree dump root=%p max_depth=%d\r\n", root, max_depth);
		return 0;
	}

	// Commands below require a device name
	if (argc < 3) {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[error] missing device name\n");

		printf_serial("[DEVMGR] missing argument for command '%s'\r\n", cmd);
		return -1;
	}

	const char* name = argv[2];
	wallos_device_t* dev = resolve_device(name);

	if (!dev) {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[error] device '%s' not found\n", name);

		printf_serial("[DEVMGR] lookup failed for '%s'\r\n", name);
		return -1;
	}

	// ------------------------------------------------------------------------
	// dev path <name>
	// ------------------------------------------------------------------------
	if (strcmp(cmd, "path") == 0) {
		if (dev->path) {
			printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "%s\n", dev->path);
			printf_serial("[DEVMGR] cached path for '%s' is '%s'\r\n", name, dev->path);
		} else {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[error] device has no path\n");
		}
		return 0;
	}

	// ------------------------------------------------------------------------
	// dev info <name>
	// ------------------------------------------------------------------------
	if (strcmp(cmd, "info") == 0) {
		const char* dev_name = dev->name ? dev->name : "unknown";

		printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "Device info:\n");
		printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "  Name: %s\n", dev_name);
		printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG, "  ID: %04x:%04x\n", dev->vendor_id, dev->device_id);

		print_device_flags(dev->interfaces);

		if (dev->parent) {
			printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG, "  Parent: %s\n", dev->parent->name ? dev->parent->name : "unknown");
		} else {
			printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG, "  Parent: <root>\n");
		}

		int child_count = 0;
		DEV_FOR_EACH_CHILD(dev, child) {
			child_count++;
		}

		printf_color(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG, "  Children: %d\n", child_count);

		// Debug dump
		printf_serial("[DEVMGR] info dev=%p name='%s'\r\n", dev, dev_name);
		printf_serial("           vid:did=%04x:%04x flags=0x%llx\r\n", dev->vendor_id, dev->device_id, (uint64_t) dev->interfaces);
		printf_serial("           parent=%p first_child=%p next_sibling=%p children=%d\r\n", dev->parent, dev->first_child, dev->next_sibling, child_count);

		return 0;
	}

	if (strcmp(cmd, "refresh") == 0) {
		recalculate_device_path(dev);
		printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "Path refreshed: %s\n", dev->path ? dev->path : "NULL");
		return 0;
	}

	printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[error] unknown command '%s'\n", cmd);

	printf_serial("[DEVMGR] unknown command '%s'\r\n", cmd);
	return -1;
}