#ifndef WALLOS_HID_CONTROLLER_H
#define WALLOS_HID_CONTROLLER_H

#include <drivers/usb/class/hid/hid_common.h>


#ifdef __cplusplus
extern "C" {
#endif

	int hid_controller_attach(usb_interface_t* iface);

#ifdef __cplusplus
}
#endif
#endif