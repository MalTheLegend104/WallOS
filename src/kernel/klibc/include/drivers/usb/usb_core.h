#ifndef WALLOS_USB_CORE_H
#define WALLOS_USB_CORE_H

#include <device/device_manager.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct wallos_device wallos_device_t;
	typedef struct usb_hcd usb_hcd_t;
	typedef struct usb_device usb_device_t;
	typedef struct usb_endpoint usb_endpoint_t;
	typedef struct usb_transfer usb_transfer_t;
	typedef struct usb_hcd_ops usb_hcd_ops_t;

	typedef enum {
		USB_SPEED_UNKNOWN = -1,
		USB_LOW_SPEED = 0,
		USB_FULL_SPEED = 1,
		USB_HIGH_SPEED,
		// USB-IF made a mess with higher speed devices in USB 3 and 4
		// They recommend using the speed rather than the name, which is what we're going to do
		// USB_SUPER_SPEED, // technically USB 3.0, or USB 3.2 Gen 2x1
		// USB_SUPER_SPEED_PLUS, // SS+ covers anything between 5Gbps to 80 Gbps...
		USB_SPEED_5GBPS,    // SuperSpeed / USB 3.0 / USB 3.2 Gen 1 (aka Gen 1x1)
		USB_SPEED_10GBPS,   // SuperSpeed+ / USB 3.1 / USB 3.2 Gen 2 (Gen 2x1 or Gen 1x2)
		USB_SPEED_20GBPS,   // USB 3.2 Gen 2x2 (SuperSpeed+ dual-lane 10Gbps)

		// Anything over 20Gbps is USB4, which is supported by xHCI but not in the free easy to find version of the spec...
		USB_SPEED_40GBPS,   // USB4 Gen 3x2 (or Thunderbolt 3/4)
		USB_SPEED_80GBPS,   // USB4 Gen 4x2 / USB4 Version 2.0 (Pulse Amplitude Modulation / PAM3)
		USB_SPEED_120GBPS,  // USB4 Version 2.0 Asymmetric mode (120 Gbps tx / 40 Gbps rx)
	} usb_speed_t;

	static inline const char* usb_speed_to_string(usb_speed_t speed) {
		switch (speed) {
			case USB_LOW_SPEED:     return "LOW SPEED";
			case USB_FULL_SPEED:    return "FULL SPEED";
			case USB_HIGH_SPEED:    return "HIGH SPEED";
			case USB_SPEED_5GBPS:   return "SS 5Gbps";
			case USB_SPEED_10GBPS:  return "SS+ 10Gbps";
			case USB_SPEED_20GBPS:  return "SS+ 20Gbps";
			case USB_SPEED_40GBPS:  return "USB4 40Gbps";
			case USB_SPEED_80GBPS:  return "USB4 80Gbps";
			case USB_SPEED_120GBPS: return "USB4 120Gbps";
			default:                return "UNKNOWN";
		}
	}

	/* Normalized port status return.
	 * Returned by get_port_status() in usb_hcd_ops_t.
	 */
	typedef struct {
		bool connected; /* True if a device is currently attached to this port */
		bool enabled;   /* True if the port is enabled (out of reset, forwarding packets) */
		usb_speed_t speed; /* negotiated device speed. only meaningful if connected */
	} usb_port_status_t;

	typedef enum {
		USB_ENDPOINT_TYPE_CONTROL = 0,
		USB_ENDPOINT_TYPE_ISOCHRONOUS,
		USB_ENDPOINT_TYPE_BULK,
		USB_ENDPOINT_TYPE_INTERRUPT,
	} usb_endpoint_type_t;

	typedef enum {
		USB_DIR_OUT = 0,
		USB_DIR_IN = 1,
	} usb_direction_t;

	typedef enum {
		USB_HCD_OHCI,
		USB_HCD_UHCI,
		USB_HCD_EHCI,
		USB_HCD_XHCI,
	} usb_hcd_type_t;

	// loosely based on section 6.4.5 of XHCI spec
	typedef enum {
		USB_TRANSFER_COMPLETED = 0,
		USB_TRANSFER_PENDING,
		USB_TRANSFER_ERROR_STALL,
		USB_TRANSFER_ERROR_BABBLE, // 4.10.2.4 of XHCI - When a device transmits more data on the USB than the host controller is expecting for a transaction
		USB_TRANSFER_ERROR_NAK,
		USB_TRANSFER_ERROR_CRC, // transfer stopped/aborted
		USB_TRANSFER_ERROR_TIMEOUT,
		USB_TRANSFER_ERROR_CANCELLED,
		USB_TRANSFER_ERROR_HARDWARE,
	} usb_transfer_status_t;

	typedef struct {
		uint8_t  bmRequestType;
		uint8_t  bRequest;
		uint16_t wValue;
		uint16_t wIndex;
		uint16_t wLength;
	} __attribute__((packed)) usb_setup_packet_t;

	typedef void (*usb_transfer_callback_t)(usb_transfer_t* transfer);

	struct usb_transfer {
		usb_device_t* device;
		usb_endpoint_t* endpoint;

		usb_setup_packet_t* setup;
		void* buffer;
		size_t length;
		size_t actual_length;

		usb_transfer_status_t status;
		uint32_t timeout_ms;

		usb_transfer_callback_t callback;
		void* context;

		void* hcd_data;
	};

	struct usb_endpoint {
		uint8_t address;
		uint8_t number;
		usb_direction_t direction;
		usb_endpoint_type_t type;
		uint16_t max_packet_size;
		uint8_t interval;

		const usb_device_t* device;
		void* hcd_data;
	};

	struct usb_device {
		wallos_device_t* device;

		usb_speed_t speed;
		uint8_t address;
		uint8_t port;

		usb_hcd_t* hcd;
		void* hcd_data;

		uint8_t configuration_value;

		usb_endpoint_t* endpoints;
		size_t endpoint_count;

		struct usb_device* parent;
	};

	struct usb_hcd {
		wallos_device_t* device;
		usb_hcd_type_t type;
		const usb_hcd_ops_t* ops;
		void* hcd_data;
	};

	struct usb_hcd_ops {
		int (*start)(usb_hcd_t* hcd);
		int (*stop)(usb_hcd_t* hcd);
		int (*reset)(usb_hcd_t* hcd);

		size_t(*get_port_count)(usb_hcd_t* hcd);
		int (*get_port_status)(usb_hcd_t* hcd, uint8_t port, usb_port_status_t* status);
		int (*reset_port)(usb_hcd_t* hcd, uint8_t port);
		int (*enable_port)(usb_hcd_t* hcd, uint8_t port);
		int (*disable_port)(usb_hcd_t* hcd, uint8_t port);

		// xHCI forces the system software to kinda just deal with whatever address it decides to give the device
		// as a result, device addresses are issues for the HC itself
		// device init should properly set the address
		int (*device_init)(usb_hcd_t* hcd, usb_device_t* dev);
		// int (*device_address)(usb_hcd_t* hcd, usb_device_t* dev, uint8_t address);
		int (*device_destroy)(usb_hcd_t* hcd, usb_device_t* dev);

		int (*endpoint_open)(usb_hcd_t* hcd, usb_endpoint_t* ep);
		int (*endpoint_close)(usb_hcd_t* hcd, usb_endpoint_t* ep);
		int (*endpoint_reset)(usb_hcd_t* hcd, usb_endpoint_t* ep);

		// Async transfer, calls transfer->callback() on completion (or failure)
		int (*submit_transfer)(usb_hcd_t* hcd, usb_transfer_t* transfer);
		// cancel only an Async transfer. Sync transfers can in theory be canceled but I don't really feel like implementing it.
		int (*cancel_transfer)(usb_hcd_t* hcd, usb_transfer_t* transfer);
		// synchronous transfer, will block until transfer is complete (or failed)
		int (*execute_transfer)(usb_hcd_t* hcd, usb_transfer_t* transfer);
	};

	void usb_init(void);

	int usb_hcd_register(usb_hcd_t* hcd);
	void usb_hcd_unregister(usb_hcd_t* hcd);

	// idk if I want the usb class drivers to have to work directly with usb_hcd_ops or not
	// I think I'm going to have them work through this interface, it lets me change the structs later without a lot of refactoring
	// synchronous transfer
	int usb_transfer_sync(usb_transfer_t* transfer);
	int usb_control_msg(usb_device_t* dev, uint8_t request_type, uint8_t request, uint16_t value, uint16_t index, void* data, uint16_t length, uint32_t timeout_ms);

	// Not really meant to be called by anything externally, but I wanted this logic in a separate file so...
	// Will first go through hcd_ops->get_port_count() ports, see which are connected (and what speed), then create devices for each
	int usb_enumerate_hcd(usb_hcd_t* hcd);
#ifdef __cplusplus
}
#endif
#endif