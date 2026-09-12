#include <drivers/usb/class/hid/hid_mouse.h>

#include <drivers/serial.h>
#include <drivers/usb/usb_core.h>

int hid_mouse_attach(usb_interface_t* iface) {
	usb_endpoint_t* ep_in = usb_find_endpoint(iface, USB_ENDPOINT_TYPE_INTERRUPT, USB_DIR_IN);
	if (!ep_in) {
		printf_serial("[HID][MOUSE] no interrupt IN endpoint on interface %u.\r\n", iface->interface_number);
		return -1;
	}

	if (iface->interface_subclass == HID_SUBCLASS_BOOT) {
		hid_set_protocol(iface, HID_PROTOCOL_BOOT);
	}

	// We don't do anything else with mice yet.
	// Input handler does technically support it, but I don't want to deal with mouse inputs right now
	printf_serial(
		"[HID][MOUSE] detected on interface %u, ep=0x%02x (polling not yet implemented)\r\n",
		iface->interface_number,
		ep_in->address
	);
	return 0;
}