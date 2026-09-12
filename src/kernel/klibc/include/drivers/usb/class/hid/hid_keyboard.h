#ifndef WALLOS_HID_KEYBOARD_H
#define WALLOS_HID_KEYBOARD_H

#include <drivers/usb/class/hid/hid_common.h>

#ifdef __cplusplus
extern "C" {
#endif

	int hid_keyboard_attach(usb_interface_t* iface);
	void hid_keyboard_poll_all(void);

#ifdef __cplusplus
}
#endif
#endif