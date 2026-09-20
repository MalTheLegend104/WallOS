#ifndef WALLOS_USB_DESCRIPTORS_H
#define WALLOS_USB_DESCRIPTORS_H

#include <stdint.h>

#ifdef __cplusplus
#ifndef _Static_assert
#define _Static_assert static_assert
#endif

extern "C" {
#endif

	/* Standard USB request codes (USB 2.0 spec, Table 9-4) */
#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE     0x0A
#define USB_REQ_SET_INTERFACE     0x0B

	/* Descriptor types (Table 9-5) */
#define USB_DESC_TYPE_DEVICE        0x01
#define USB_DESC_TYPE_CONFIGURATION 0x02
#define USB_DESC_TYPE_STRING        0x03
#define USB_DESC_TYPE_INTERFACE     0x04
#define USB_DESC_TYPE_ENDPOINT      0x05

	/* bmRequestType bit definitions */
#define USB_REQTYPE_DIR_OUT        (0 << 7)
#define USB_REQTYPE_DIR_IN         (1 << 7)
#define USB_REQTYPE_TYPE_STANDARD  (0 << 5)
#define USB_REQTYPE_TYPE_CLASS     (1 << 5)
#define USB_REQTYPE_TYPE_VENDOR    (2 << 5)
#define USB_REQTYPE_RECIP_DEVICE    0x00
#define USB_REQTYPE_RECIP_INTERFACE 0x01
#define USB_REQTYPE_RECIP_ENDPOINT  0x02

	typedef struct {
		uint8_t  bLength;
		uint8_t  bDescriptorType;
		uint16_t bcdUSB;
		uint8_t  bDeviceClass;
		uint8_t  bDeviceSubClass;
		uint8_t  bDeviceProtocol;
		uint8_t  bMaxPacketSize0;
		uint16_t idVendor;
		uint16_t idProduct;
		uint16_t bcdDevice;
		uint8_t  iManufacturer;
		uint8_t  iProduct;
		uint8_t  iSerialNumber;
		uint8_t  bNumConfigurations;
	} __attribute__((packed)) usb_device_descriptor_t;

	typedef struct {
		uint8_t  bLength;
		uint8_t  bDescriptorType;
		uint16_t wTotalLength;
		uint8_t  bNumInterfaces;
		uint8_t  bConfigurationValue;
		uint8_t  iConfiguration;
		uint8_t  bmAttributes;
		uint8_t  bMaxPower;
	} __attribute__((packed)) usb_config_descriptor_t;

	typedef struct {
		uint8_t  bLength;
		uint8_t  bDescriptorType;
		uint8_t  bInterfaceNumber;
		uint8_t  bAlternateSetting;
		uint8_t  bNumEndpoints;
		uint8_t  bInterfaceClass;
		uint8_t  bInterfaceSubClass;
		uint8_t  bInterfaceProtocol;
		uint8_t  iInterface;
	} __attribute__((packed)) usb_interface_descriptor_t;

	typedef struct {
		uint8_t  bLength;
		uint8_t  bDescriptorType;
		uint8_t  bEndpointAddress;
		uint8_t  bmAttributes;
		uint16_t wMaxPacketSize;
		uint8_t  bInterval;
	} __attribute__((packed)) usb_endpoint_descriptor_t;

	/* USB 2.0 descriptor sizes */
	_Static_assert(sizeof(usb_device_descriptor_t) == 18, "USB device descriptor must be 18 bytes");
	_Static_assert(sizeof(usb_config_descriptor_t) == 9, "USB configuration descriptor must be 9 bytes");
	_Static_assert(sizeof(usb_interface_descriptor_t) == 9, "USB interface descriptor must be 9 bytes");
	_Static_assert(sizeof(usb_endpoint_descriptor_t) == 7, "USB endpoint descriptor must be 7 bytes");

#ifdef __cplusplus
}
#endif
#endif