#include <drivers/usb/usb_core.h>
#include <device/device_manager.h>
#include <drivers/driver_manager.h>
#include <drivers/usb/usb_descriptors.h>

#include <memory/kernel_alloc.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <drivers/serial.h>

typedef struct usb_interface_node {
	usb_interface_t* iface;
	struct usb_interface_node* next;
} usb_interface_node_t;

typedef struct usb_device_node {
	usb_device_t* dev;
	struct usb_device_node* next;
} usb_device_node_t;

typedef struct {
	uint8_t interface_number;
	uint8_t interface_class;
	uint8_t interface_subclass;
	uint8_t interface_protocol;
	size_t ep_start; // index into dev->endpoints
	size_t ep_count;
} usb_interface_desc_t;

static usb_device_node_t* usb_dev_list = NULL;
static usb_interface_node_t* usb_interfaces = NULL;

static int usb_device_list_add(usb_device_t* dev) {
	usb_device_node_t* node = (usb_device_node_t*) kcalloc(1, sizeof(usb_device_node_t));
	if (!node) return -1;

	node->dev = dev;
	node->next = usb_dev_list;
	usb_dev_list = node;
	return 0;
}

static void usb_device_list_remove(usb_device_t* dev) {
	usb_device_node_t** cur = &usb_dev_list;
	while (*cur) {
		if ((*cur)->dev == dev) {
			usb_device_node_t* dead = *cur;
			*cur = dead->next;
			kfree(dead);
			return;
		}
		cur = &(*cur)->next;
	}
}

usb_device_t* usb_device_find_by_address(usb_hcd_t* hcd, uint8_t address) {
	for (usb_device_node_t* n = usb_dev_list; n; n = n->next) {
		if (n->dev->hcd == hcd && n->dev->address == address) return n->dev;
	}
	return NULL;
}

static int usb_interface_list_add(usb_interface_t* iface) {
	usb_interface_node_t* node = (usb_interface_node_t*) kcalloc(1, sizeof(usb_interface_node_t));
	if (!node) return -1;
	node->iface = iface;
	node->next = usb_interfaces;
	usb_interfaces = node;
	return 0;
}

usb_interface_t* usb_interface_from_device(wallos_device_t* wdev) {
	for (usb_interface_node_t* n = usb_interfaces; n; n = n->next) {
		if (n->iface->device == wdev) return n->iface;
	}
	return NULL;
}

/**
 * @brief Maps usb interface class to the device managers interfaces.
 * If there is no match, it returns DEV_INT_NONE.
 * Any matching to those devices should be done via [vendor_id:device_id]
 */
static device_interface_flags_t usb_class_to_interface_flag(uint8_t bInterfaceClass) {
	switch (bInterfaceClass) {
		case 0x01: return DEV_INT_AUDIO;  /* Audio */
		case 0x02: return DEV_INT_CDC;    /* CDC Control */
		case 0x03: return DEV_INT_HID;    /* Human Interface Device */
		case 0x08: return DEV_INT_MSC;    /* Mass Storage */
		case 0x0A: return DEV_INT_CDC;    /* CDC Data */
		case 0x0E: return DEV_INT_VIDEO;  /* Video */
		default:   return DEV_INT_NONE;
	}
}

// https://www.usb.org/defined-class-codes
static const char* usb_interface_name_prefix(uint8_t cls, uint8_t subclass, uint8_t protocol) {
	switch (cls) {
		case 0x03: // HID
			if (subclass == 1) { // Boot Interface Subclass
				if (protocol == 1) return "keyboard";
				if (protocol == 2) return "mouse";
			}
			return "hid";
		case 0x08: return "storage";
		case 0x01: return "audio";
		case 0x0E: return "video";
		case 0x02:
		case 0x0A: return "serial";
		default:   return "generic";
	}
}

typedef struct {
	const char* prefix;
	unsigned count;
} usb_name_counter_t;

static char* usb_build_interface_name(usb_name_counter_t* counters, size_t* counter_count, size_t max_counters, uint8_t cls, uint8_t subclass, uint8_t protocol) {
	const char* prefix = usb_interface_name_prefix(cls, subclass, protocol);

	unsigned* count = NULL;
	for (size_t i = 0; i < *counter_count; i++) {
		if (strcmp(counters[i].prefix, prefix) == 0) {
			count = &counters[i].count;
			break;
		}
	}
	if (!count && *counter_count < max_counters) {
		counters[*counter_count].prefix = prefix;
		counters[*counter_count].count = 0;
		count = &counters[*counter_count].count;
		(*counter_count)++;
	}

	char buf[24];
	snprintf(buf, sizeof(buf), "%s%u", prefix, count ? *count : 0);
	if (count) (*count)++;

	size_t len = strlen(buf) + 1;
	char* name = (char*) kcalloc(1, len);
	if (name) memcpy(name, buf, len);
	return name;
}

static char* usb_build_port_name(usb_device_t* dev) {
	char buf[24];
	snprintf(buf, sizeof(buf), "port%u", dev->port);

	size_t len = strlen(buf) + 1;
	char* name = (char*) kcalloc(1, len);
	if (name) memcpy(name, buf, len);
	return name; // caller owns this
}

static usb_endpoint_type_t usb_endpoint_type_from_attributes(uint8_t bmAttributes) {
	switch (bmAttributes & 0x03) {
		case 0: return USB_ENDPOINT_TYPE_CONTROL;
		case 1: return USB_ENDPOINT_TYPE_ISOCHRONOUS;
		case 2: return USB_ENDPOINT_TYPE_BULK;
		case 3: return USB_ENDPOINT_TYPE_INTERRUPT;
	}
	return USB_ENDPOINT_TYPE_CONTROL; // unreachable
}

static int usb_read_device_info(usb_device_t* dev, usb_device_descriptor_t* desc_out) {
	int ret = usb_control_msg(dev, 0x80, USB_REQ_GET_DESCRIPTOR, (USB_DESC_TYPE_DEVICE << 8) | 0, 0, desc_out, sizeof(*desc_out), 1000);
	if (ret != (int) sizeof(*desc_out)) {
		printf_serial("[USB] Failed to read device descriptor (ret=%d).\r\n", ret);
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] Failed to read device descriptor (ret=%d)\n", ret);
		return -1;
	}

	printf_serial("\tDevice: VID=%04x PID=%04x class=%02x subclass=%02x proto=%02x maxpkt0=%u configs=%u\r\n",
		desc_out->idVendor,
		desc_out->idProduct,
		desc_out->bDeviceClass,
		desc_out->bDeviceSubClass,
		desc_out->bDeviceProtocol,
		desc_out->bMaxPacketSize0,
		desc_out->bNumConfigurations
	);

	// the full printf_serial above has a bit more debug info that didn't fit nicely on a single line on 1024x786
	printf_color(PRINT_COLOR_LIGHT_BLUE, PRINT_DEFAULT_BG, "\tDevice: VID=%04x PID=%04x class=%02x subclass=%02x proto=%02x\n",
		desc_out->idVendor,
		desc_out->idProduct,
		desc_out->bDeviceClass,
		desc_out->bDeviceSubClass,
		desc_out->bDeviceProtocol
	);

	uint16_t actual_mps0;
	if (dev->speed >= USB_SPEED_5GBPS) {
		// SS/SS+ encodes MPS0 as an exponent. it's 2^9 rather than exactly 512
		actual_mps0 = 1u << desc_out->bMaxPacketSize0;
	} else {
		// LS/FS/HS encode it as a literal byte count 
		actual_mps0 = desc_out->bMaxPacketSize0;
	}

	if (actual_mps0 != dev->endpoints[0].max_packet_size) {
		printf_serial("[USB][WARN] EP0 max packet mismatch: HC assumed %u, device reports %u (raw field=%u). MUST BE FIXED BY HC\r\n", dev->endpoints[0].max_packet_size, actual_mps0, desc_out->bMaxPacketSize0);
		printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "[USB][WARN] EP0 max packet mismatch: HC assumed %u, device reports %u. MUST BE FIXED BY HC\r\n", dev->endpoints[0].max_packet_size, desc_out->bMaxPacketSize0);
	}

	return 0;
}

static int usb_read_config_and_open_endpoints(usb_device_t* dev, usb_interface_desc_t** ifaces_out, size_t* iface_count_out) {
	*ifaces_out = NULL;
	*iface_count_out = 0;

	usb_config_descriptor_t cfg_header;
	int ret = usb_control_msg(dev, 0x80, USB_REQ_GET_DESCRIPTOR, (USB_DESC_TYPE_CONFIGURATION << 8) | 0, 0, &cfg_header, sizeof(cfg_header), 1000);
	if (ret != (int) sizeof(cfg_header)) {
		printf_serial("[USB] Failed to read config descriptor header (ret=%d).\r\n", ret);
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] Failed to read config descriptor header (ret=%d)\n", ret);
		return -1;
	}

	uint16_t total_len = cfg_header.wTotalLength;
	if (total_len < sizeof(cfg_header)) {
		printf_serial("[USB] Config descriptor reports impossible wTotalLength=%u.\r\n", total_len);
		return -1;
	}

	uint8_t* buf = (uint8_t*) kcalloc(1, total_len);
	if (!buf) return -1;

	ret = usb_control_msg(dev, 0x80, USB_REQ_GET_DESCRIPTOR, (USB_DESC_TYPE_CONFIGURATION << 8) | 0, 0, buf, total_len, 1000);
	if (ret != (int) total_len) {
		printf_serial("[USB] Failed to read full config descriptor (ret=%d, wanted %u).\r\n", ret, total_len);
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] Failed to read full config descriptor (ret=%d, wanted %u)\n", ret, total_len);
		kfree(buf);
		return -1;
	}

	// Pass 1
	// count interfaces (alt-setting 0 only) and endpoints under them.
	size_t iface_count = 0;
	size_t ep_count = 0;
	uint16_t off = cfg_header.bLength;
	bool in_active_interface = false;

	while (off + 2 <= total_len) {
		uint8_t blen = buf[off];
		uint8_t btype = buf[off + 1];
		if (blen < 2 || off + blen > total_len) break;

		if (btype == USB_DESC_TYPE_INTERFACE) {
			usb_interface_descriptor_t* iface = (usb_interface_descriptor_t*) &buf[off];
			in_active_interface = (iface->bAlternateSetting == 0);
			if (in_active_interface) iface_count++;
		} else if (btype == USB_DESC_TYPE_ENDPOINT && in_active_interface) {
			ep_count++;
		}
		off += blen;
	}

	if (iface_count == 0) {
		printf_serial("[USB][WARN] Config descriptor has no usable (alt-setting 0) interfaces.\r\n");
		printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "[USB][WARN] Config descriptor has no usable (alt-setting 0) interfaces.\n");
		kfree(buf);
		return -1;
	}

	usb_interface_desc_t* ifaces = (usb_interface_desc_t*) kcalloc(iface_count, sizeof(usb_interface_desc_t));
	usb_endpoint_t* new_endpoints = (usb_endpoint_t*) kcalloc(1 + ep_count, sizeof(usb_endpoint_t));
	if (!ifaces || !new_endpoints) {
		kfree(ifaces);
		kfree(new_endpoints);
		kfree(buf);
		return -1;
	}
	new_endpoints[0] = dev->endpoints[0]; // preserve EP0

	// Pass 2
	// populate interfaces + endpoints together
	size_t ep_idx = 1;
	size_t if_idx = 0;
	off = cfg_header.bLength;
	in_active_interface = false;
	usb_interface_desc_t* cur_iface = NULL;

	while (off + 2 <= total_len) {
		uint8_t blen = buf[off];
		uint8_t btype = buf[off + 1];
		if (blen < 2 || off + blen > total_len) break;

		if (btype == USB_DESC_TYPE_INTERFACE) {
			usb_interface_descriptor_t* iface = (usb_interface_descriptor_t*) &buf[off];
			in_active_interface = (iface->bAlternateSetting == 0);

			if (in_active_interface) {
				cur_iface = &ifaces[if_idx++];
				cur_iface->interface_number = iface->bInterfaceNumber;
				cur_iface->interface_class = iface->bInterfaceClass;
				cur_iface->interface_subclass = iface->bInterfaceSubClass;
				cur_iface->interface_protocol = iface->bInterfaceProtocol;
				cur_iface->ep_start = ep_idx;
				cur_iface->ep_count = 0;
			} else {
				cur_iface = NULL;
			}
		} else if (btype == USB_DESC_TYPE_ENDPOINT && in_active_interface && cur_iface) {
			usb_endpoint_descriptor_t* epd = (usb_endpoint_descriptor_t*) &buf[off];

			usb_endpoint_t* ep = &new_endpoints[ep_idx++];
			ep->address = epd->bEndpointAddress;
			ep->number = epd->bEndpointAddress & 0x0F;
			ep->direction = (epd->bEndpointAddress & 0x80) ? USB_DIR_IN : USB_DIR_OUT;
			ep->type = usb_endpoint_type_from_attributes(epd->bmAttributes);
			ep->max_packet_size = epd->wMaxPacketSize & 0x07FF;
			ep->interval = epd->bInterval;
			ep->device = dev;
			ep->hcd_data = NULL;

			if (dev->hcd->ops->endpoint_open(dev->hcd, ep) != 0) {
				printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] Failed to open endpoint 0x%02x.\r\n", ep->address);
				printf_serial("[USB][ERROR] Failed to open endpoint 0x%02x.\r\n", ep->address);
			}
			cur_iface->ep_count++;
		}
		off += blen;
	}

	kfree(dev->endpoints); // the EP0-only array device_init allocated
	dev->endpoints = new_endpoints;
	dev->endpoint_count = ep_idx;

	ret = usb_control_msg(dev, 0x00, USB_REQ_SET_CONFIGURATION, cfg_header.bConfigurationValue, 0, NULL, 0, 1000);
	if (ret < 0) {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] SET_CONFIGURATION failed.\r\n");
		printf_serial("[USB][ERROR] SET_CONFIGURATION failed.\r\n");
		kfree(ifaces);
		kfree(buf);
		return -1;
	}
	dev->configuration_value = cfg_header.bConfigurationValue;

	kfree(buf);
	*ifaces_out = ifaces;
	*iface_count_out = if_idx;
	printf_serial("[USB] Device configured: %zu interfaces, %zu endpoints active.\r\n", if_idx, dev->endpoint_count);
	return 0;
}

static int usb_enumerate_device(usb_hcd_t* hcd, uint8_t port, usb_speed_t speed) {
	// printf("[USB] Enumerating port %u (%s)\n", port, usb_speed_to_string(speed));
	usb_device_t* dev = (usb_device_t*) kcalloc(1, sizeof(usb_device_t));
	if (!dev) return -1;

	dev->hcd = hcd;
	dev->speed = speed;
	dev->port = port;
	dev->address = 0;
	dev->parent = NULL;

	if (hcd->ops->device_init(hcd, dev) != 0) {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] Device initialization failed on port %u\r\n", port);
		printf_serial("[USB][ERROR] Device initialization failed on port %u\r\n", port);
		kfree(dev);
		return -1;
	}


	usb_device_descriptor_t desc;
	if (usb_read_device_info(dev, &desc) != 0) {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] Failed to read device information on port %u\r\n", port);
		printf_serial("[USB][ERROR] Failed to read device information on port %u\r\n", port);
		hcd->ops->device_destroy(hcd, dev);
		kfree(dev);
		return -1;
	}


	usb_interface_desc_t* ifaces = NULL;
	size_t iface_count = 0;
	if (usb_read_config_and_open_endpoints(dev, &ifaces, &iface_count) != 0) {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] Failed to configure device on port %u\r\n", port);
		printf_serial("[USB][ERROR] Failed to configure device on port %u\r\n", port);
		hcd->ops->device_destroy(hcd, dev);
		kfree(dev);
		return -1;
	}


	/* Port node is a container. Class info lives on the interfaces below. */
	wallos_device_t* port_dev = (wallos_device_t*) kcalloc(1, sizeof(wallos_device_t));
	if (!port_dev) {
		kfree(ifaces);
		hcd->ops->device_destroy(hcd, dev);
		kfree(dev);
		return -1;
	}

	port_dev->name = usb_build_port_name(dev);
	port_dev->interfaces = DEV_INT_USB;
	port_dev->vendor_id = desc.idVendor;
	port_dev->device_id = desc.idProduct;
	port_dev->subsystem_id = desc.bcdDevice;
	port_dev->location.usb.port = dev->port;
	port_dev->location.usb.address = dev->address;
	port_dev->parent = dev->parent ? dev->parent->device : hcd->device;

	dev->device = port_dev;
	register_device(port_dev);


	/* Per-interface child nodes */
	usb_name_counter_t counters[8];
	size_t counter_count = 0;

	for (size_t i = 0; i < iface_count; i++) {
		usb_interface_desc_t* id = &ifaces[i];

		printf_color(PRINT_COLOR_LIGHT_BLUE, PRINT_DEFAULT_BG, "\tInterface %u: class=%02x subclass=%02x protocol=%02x endpoints=%zu\n", id->interface_number, id->interface_class, id->interface_subclass, id->interface_protocol, id->ep_count);

		usb_interface_t* iface = (usb_interface_t*) kcalloc(1, sizeof(usb_interface_t));
		wallos_device_t* iface_dev = (wallos_device_t*) kcalloc(1, sizeof(wallos_device_t));
		if (!iface || !iface_dev) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] Failed to allocate interface % u node.\r\n", id->interface_number);
			printf_serial("[USB][ERROR] Failed to allocate interface %u node.\r\n", id->interface_number);
			kfree(iface);
			kfree(iface_dev);
			continue; // don't fail the whole device over one interface node
		}

		iface->usb_dev = dev;
		iface->interface_number = id->interface_number;
		iface->interface_class = id->interface_class;
		iface->interface_subclass = id->interface_subclass;
		iface->interface_protocol = id->interface_protocol;
		iface->endpoints = &dev->endpoints[id->ep_start];
		iface->endpoint_count = id->ep_count;

		iface_dev->name = usb_build_interface_name(counters, &counter_count, 8, id->interface_class, id->interface_subclass, id->interface_protocol);
		iface_dev->interfaces = DEV_INT_USB | usb_class_to_interface_flag(id->interface_class);
		iface_dev->vendor_id = desc.idVendor;
		iface_dev->device_id = desc.idProduct;
		iface_dev->subsystem_id = desc.bcdDevice;
		iface_dev->location.usb.port = dev->port;
		iface_dev->location.usb.address = dev->address;
		iface_dev->parent = port_dev;

		iface->device = iface_dev;
		register_device(iface_dev);

		if (usb_interface_list_add(iface) != 0) {
			printf_serial("[USB][WARN] Failed to track interface %u internally.\r\n", id->interface_number);
		}
	}

	kfree(ifaces);

	if (usb_device_list_add(dev) != 0) {
		hcd->ops->device_destroy(hcd, dev);
		kfree(dev);
		return -1;
	}

	// printf("[USB] Enumeration complete: port %u address %u, %zu interfaces, %zu endpoints\n", port, dev->address, iface_count, dev->endpoint_count);

	return 0;
}

int usb_enumerate_hcd(usb_hcd_t* hcd) {
	if (!hcd || !hcd->ops) {
		printf("[USB] Failed to enumerate HCD: invalid HCD or operations\n");
		return -1;
	}

	size_t port_count = hcd->ops->get_port_count(hcd);
	printf_color(PRINT_COLOR_LIGHT_BLUE, PRINT_DEFAULT_BG, "[USB] Enumerating %zu ports on %s\n", port_count, hcd->device->name);

	for (uint8_t port = 0; port < (uint8_t) port_count; port++) {
		usb_port_status_t status;
		memset(&status, 0, sizeof(status));

		// printf("[USB] Checking port %u\n", port);

		if (hcd->ops->get_port_status(hcd, port, &status) != 0) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] Port %u: failed to get port status\n", port);
			continue;
		}

		if (!status.connected) {
			// printf("[USB] Port %u: no device connected\n", port);
			continue;
		}

		// printf("[USB] Port %u: device connected\n", port);
		int a = hcd->ops->reset_port(hcd, port);
		if (a != 0) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] failed to reset port (%d) \n", port, a);
			continue;
		}

		// printf("[USB] Port %u: reset successful\n", port);

		if (hcd->ops->enable_port(hcd, port) != 0) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] Port %u: failed to enable port\n", port);
			continue;
		}

		// printf("[USB] Port %u: enabled successfully\n", port);

		// According to the xHCI spec we need to re-read after reset to get the proper speed.
		// I'm not sure if this applies to  the other HCs but it can't hurt.
		if (hcd->ops->get_port_status(hcd, port, &status) != 0) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] Port %u: failed to re-read port status\n", port);
			continue;
		}

		if (!status.connected) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] Port %u: device disconnected after reset\n", port);
			continue;
		}

		if (!status.enabled) {
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] Port %u: port not enabled after reset\n", port);
			continue;
		}

		printf_color(PRINT_COLOR_LIGHT_BLUE, PRINT_DEFAULT_BG, "[USB] Port %u has a %s device\n", port, usb_speed_to_string_display(status.speed));

		if (usb_enumerate_device(hcd, port, status.speed) != 0) {
			// printf("[USB] Port %u: device enumeration failed\n", port);
			continue;
		}

		// printf("[USB] Port %u: device enumeration successful\n", port);
	}

	return 0;
}