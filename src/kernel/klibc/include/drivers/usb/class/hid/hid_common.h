#ifndef WALLOS_HID_COMMON_H
#define WALLOS_HID_COMMON_H

#include <drivers/usb/usb_core.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_CLASS_HID 0x03

#define HID_SUBCLASS_BOOT 0x01

#define HID_BOOT_PROTOCOL_NONE     0x00
#define HID_BOOT_PROTOCOL_KEYBOARD 0x01
#define HID_BOOT_PROTOCOL_MOUSE    0x02

// Class-specific requests
// USB HID spec 1.11, section 7.2
#define HID_REQ_GET_REPORT   0x01
#define HID_REQ_GET_IDLE     0x02
#define HID_REQ_GET_PROTOCOL 0x03
#define HID_REQ_SET_REPORT   0x09
#define HID_REQ_SET_IDLE     0x0A
#define HID_REQ_SET_PROTOCOL 0x0B

// bmRequestType: host-to-device or device-to-host | class-specific | interface recipient
// USB HID spec Table 9-2
#define HID_REQTYPE_CLASS_OUT 0x21
#define HID_REQTYPE_CLASS_IN  0xA1

// SET_PROTOCOL wValue
// USB HID spec 7.2.6
// Not to be confused with usb_interface_t's interface_protocol, which identifies boot keyboard/mouse.
#define HID_PROTOCOL_BOOT   0
#define HID_PROTOCOL_REPORT 1

// Descriptor types within a HID interface
// USB HID spec 7.1
#ifndef USB_DESC_TYPE_HID_REPORT
#define USB_DESC_TYPE_HID_REPORT 0x22
#endif

	typedef enum {
		HID_DEVICE_KEYBOARD,
		HID_DEVICE_MOUSE,
		HID_DEVICE_CONTROLLER,
		HID_DEVICE_UNKNOWN,
	} hid_device_kind_t;

	/**
	 * @brief Classifies a HID interface as keyboard/mouse/controller/unknown.
	 */
	hid_device_kind_t hid_classify_interface(usb_interface_t* iface);

	/**
	 * @brief HID class-specific SET_IDLE.
	 * duration_4ms == 0 means "report only on change".
	 */
	int hid_set_idle(usb_interface_t* iface, uint8_t duration_4ms);

	/**
	 * @brief HID class-specific SET_PROTOCOL (HID_PROTOCOL_BOOT / HID_PROTOCOL_REPORT).
	 * Only meaningful on boot-subclass interfaces.
	 */
	int hid_set_protocol(usb_interface_t* iface, uint8_t protocol);

	/* One entry per sub-driver */

	int hid_keyboard_attach(usb_interface_t* iface);
	int hid_mouse_attach(usb_interface_t* iface);
	int hid_controller_attach(usb_interface_t* iface);

	void hid_keyboard_poll_all(void);
#ifdef __cplusplus
}
#endif
#endif