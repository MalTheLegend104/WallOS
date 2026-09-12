#include <drivers/usb/class/hid/hid_controller.h>

#include <drivers/serial.h>

int hid_controller_attach(usb_interface_t* iface) {
	// hid_classify_interface() parsed enough of the report descriptor to identify this device.
	// We need to fully parse it to determine each button/axis bit offset, since these layouts vary by device
	// Controller support is LONG ways away, if ever, I just wanted us to be able to actually "bind" them since we bind all generic HID devices
	// If we can determine that it's a controller as opposed to a generic HID device, that's a win in my book
	printf_serial(
		"[HID][CTRL] HID device on interface %u (class=%02x sub=%02x proto=%02x) (controller support is not implemented)\r\n",
		iface->interface_number,
		iface->interface_class,
		iface->interface_subclass,
		iface->interface_protocol
	);
	return 0;
}