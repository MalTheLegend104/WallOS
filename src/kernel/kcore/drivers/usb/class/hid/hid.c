#include <device/device_manager.h>
#include <drivers/usb/class/hid/hid_common.h>

#include <drivers/driver_manager.h>
#include <drivers/serial.h>
#include <drivers/usb/usb_core.h>
#include <drivers/usb/usb_descriptors.h>

#include <stdbool.h>
#include <string.h>

static bool hid_get_report_descriptor_length(usb_interface_t* iface, uint16_t* length_out) {
	if (!iface || !iface->class_descriptors || iface->class_descriptors_length < 9) return false;

	const uint8_t* d = iface->class_descriptors;
	if (d[1] != 0x21) return false; // not a HID descriptor

	uint8_t num_descriptors = d[5];
	size_t offset = 6;
	for (uint8_t i = 0; i < num_descriptors; i++) {
		if (offset + 3 > iface->class_descriptors_length) break;
		uint8_t desc_type = d[offset];
		uint16_t desc_len = d[offset + 1] | (d[offset + 2] << 8);
		if (desc_type == USB_DESC_TYPE_HID_REPORT) {
			if (length_out) *length_out = desc_len;
			return true;
		}
		offset += 3;
	}
	return false;
}

int hid_probe(wallos_device_t* wdev) {
	if (!usb_is_valid_device(wdev)) return 1;

	usb_interface_t* iface = usb_interface_from_device(wdev);
	if (iface && iface->interface_class == USB_CLASS_HID) return 0;

	return 0;
}

void hid_attach(wallos_device_t* wdev) {
	usb_interface_t* iface = usb_interface_from_device(wdev);
	if (!iface || iface->interface_class != USB_CLASS_HID) {
		return;
	}

	hid_device_kind_t kind = hid_classify_interface(iface);

	switch (kind) {
		case HID_DEVICE_KEYBOARD:
			hid_keyboard_attach(iface);
			return;
		case HID_DEVICE_MOUSE:
			hid_mouse_attach(iface);
			return;
		case HID_DEVICE_CONTROLLER:
			hid_controller_attach(iface);
			return;
		default:
			printf_serial(
				"[HID] interface %u (sub=%02x proto=%02x) didn't classify as keyboard/mouse/controller; treating as generic.\r\n",
				iface->interface_number,
				iface->interface_subclass,
				iface->interface_protocol
			);
			return;
	}
}

int hid_set_idle(usb_interface_t* iface, uint8_t duration_4ms) {
	if (!iface) return -1;
	return usb_control_msg(iface->usb_dev, HID_REQTYPE_CLASS_OUT, HID_REQ_SET_IDLE, ((uint16_t) duration_4ms << 8), iface->interface_number, NULL, 0, 1000);
}

int hid_set_protocol(usb_interface_t* iface, uint8_t protocol) {
	if (!iface) return -1;
	return usb_control_msg(iface->usb_dev, HID_REQTYPE_CLASS_OUT, HID_REQ_SET_PROTOCOL, protocol, iface->interface_number, NULL, 0, 1000);
}

#define HID_REPORT_DESC_MAX 256

hid_device_kind_t hid_classify_interface(usb_interface_t* iface) {
	if (!iface) return HID_DEVICE_UNKNOWN;

	// Fast path: boot-protocol interfaces self-identify, no descriptor read needed.
	if (iface->interface_subclass == HID_SUBCLASS_BOOT) {
		if (iface->interface_protocol == HID_BOOT_PROTOCOL_KEYBOARD) return HID_DEVICE_KEYBOARD;
		if (iface->interface_protocol == HID_BOOT_PROTOCOL_MOUSE) return HID_DEVICE_MOUSE;
	}

	// Slow path: most gamepads/joysticks (and non-boot keyboards/mice)
	uint16_t report_len = 0;
	if (!hid_get_report_descriptor_length(iface, &report_len) || report_len == 0) {
		report_len = HID_REPORT_DESC_MAX; // HID descriptor wasn't captured or wasn't parseable. we make a guess as a last resort
	}
	if (report_len > HID_REPORT_DESC_MAX) report_len = HID_REPORT_DESC_MAX; // buf below is a fixed-size stack buffer.
																			// I doubt anything will give a report larger than this, but still want to guard against it

	uint8_t buf[HID_REPORT_DESC_MAX];
	int ret = usb_control_msg(iface->usb_dev, 0x81, USB_REQ_GET_DESCRIPTOR, (USB_DESC_TYPE_HID_REPORT << 8), iface->interface_number, buf, report_len, 1000);
	if (ret <= 0) {
		printf_serial("[HID] failed to read report descriptor on interface %u (ret=%d).\r\n", iface->interface_number, ret);
		return HID_DEVICE_UNKNOWN;
	}

	// Minimal scanner used only to find the first top-level Application Collection's Usage Page/Usage pair
	// We ignore Push/Pop, Report IDs, and nested collections after the first Application Collection

	uint16_t usage_page = 0, usage = 0;
	bool have_usage_page = false, have_usage = false;

	size_t off = 0;
	while (off < (size_t) ret) {
		uint8_t header = buf[off];

		if (header == 0xFE) { // long item - skip it entirely
			if (off + 1 >= (size_t) ret) break;
			uint8_t long_data_size = buf[off + 1];
			off += 3 + long_data_size;
			continue;
		}

		uint8_t size_code = header & 0x03;
		uint8_t type = (header >> 2) & 0x03; // 0 = Main, 1 = Global, 2 = Local, 3 = Reserved
		uint8_t tag = (header >> 4) & 0x0F;
		uint8_t data_size = (size_code == 3) ? 4 : size_code; // size code 3 means 4 bytes, HID spec 6.2.2.2

		if (off + 1 + data_size > (size_t) ret) break;

		uint32_t data = 0;
		for (uint8_t i = 0; i < data_size; i++) {
			data |= ((uint32_t) buf[off + 1 + i]) << (8 * i);
		}

		if (type == 1 && tag == 0x0) { // Global: Usage Page
			usage_page = (uint16_t) data;
			have_usage_page = true;
		} else if (type == 2 && tag == 0x0) { // Local: Usage
			usage = (uint16_t) data;
			have_usage = true;
		} else if (type == 0 && tag == 0xA) { // Main: Collection
			if (data == 0x01 && have_usage_page && have_usage) { // Application collection
				if (usage_page == 0x01) { // Generic Desktop page
					if (usage == 0x06) return HID_DEVICE_KEYBOARD;
					if (usage == 0x02) return HID_DEVICE_MOUSE;
					if (usage == 0x04 || usage == 0x05) return HID_DEVICE_CONTROLLER; // Joystick / Gamepad
				}
				break; // stop at the first Application collection either way
			}
		}

		off += 1 + data_size;
	}

	return HID_DEVICE_UNKNOWN;
}

void hid_detach(wallos_device_t* dev) {
	(void) dev;
}

static wallos_driver_t hid_driver = {
	.name = "HID",
	.match_flags = DEV_INT_USB | DEV_INT_HID,
	.match_mask = DEV_INT_MASK_CONTROLLER | DEV_INT_MASK_TRANSPORT | DEV_INT_MASK_PROTOCOL,

	// vendor id and device id are unused. vendor/dev binding takes priority over this anyway if an HID device needs a specific driver
	.vendor_id = 0,
	.device_id = 0,

	.ops = {
		.attach = hid_attach,
		.probe = hid_probe,
		.detach = hid_detach, // no hot-unplug support yet
	}
};

void hid_init(void) {
	printf_serial("[HID] Registered Driver\r\n");
	dm_register_driver(&hid_driver);
}