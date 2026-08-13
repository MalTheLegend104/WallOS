#include <drivers/usb/usb_core.h>
#include <device/device_manager.h>
#include <drivers/driver_manager.h>
#include <drivers/usb/usb_descriptors.h>

#include <memory/kernel_alloc.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>


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


static int usb_enumerate_device(usb_hcd_t* hcd, uint8_t port, usb_speed_t speed) {
	usb_device_t* dev = (usb_device_t*) kcalloc(1, sizeof(usb_device_t));
	if (!dev) return -1;

	dev->hcd = hcd;
	dev->speed = speed;
	dev->port = port;
	dev->address = 0;
	dev->parent = NULL;

	if (hcd->ops->device_init(hcd, dev) != 0) {
		kfree(dev);
		return -1;
	}

	return 0;
}

int usb_enumerate_hcd(usb_hcd_t* hcd) {
	if (!hcd || !hcd->ops) return -1;

	size_t port_count = hcd->ops->get_port_count(hcd);

	for (uint8_t port = 0; port < (uint8_t) port_count; port++) {
		usb_port_status_t status;
		memset(&status, 0, sizeof(status));

		if (hcd->ops->get_port_status(hcd, port, &status) != 0) continue;
		if (!status.connected) continue;

		if (hcd->ops->reset_port(hcd, port) != 0) continue;
		if (hcd->ops->enable_port(hcd, port) != 0) continue;

		// According to the xHCI spec we need to re-read after reset to get the proper speed
		// I'm not sure if this applies to the other HCs but it cant hurt...
		if (hcd->ops->get_port_status(hcd, port, &status) != 0) continue;
		if (!status.connected || !status.enabled) continue;

		printf_color(PRINT_COLOR_PURPLE, PRINT_DEFAULT_BG, "[USB] %s port %u has a %s device\n", hcd->device->name, port, usb_speed_to_string(status.speed));

		usb_enumerate_device(hcd, port, status.speed);
	}

	return 0;
}
