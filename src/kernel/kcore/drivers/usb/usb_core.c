#include <drivers/usb/usb_core.h>

#include <memory/kernel_alloc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* We keep a list of registered HCDs for convience.
 * This will probably get a lot more use whenever we end up actually implementing class drivers
 */
typedef struct hcd_list_entry {
	usb_hcd_t* hcd;
	struct hcd_list_entry* next;
} hcd_list_entry_t;

// This grows backwards instead of forwards.
// This was oversight on me writing it at 2AM, but don't feel like going back and changing it
// the "newest" registered host controller will be pointed to by this, the oldest is at the end of the chain
static hcd_list_entry_t* hcd_list = NULL;

static bool validate_hcd(usb_hcd_t* hcd) {
	// for sanity sake, we need to ensure that the most important ops are filled in
	if (!hcd->ops) return false;

	// start, stop, and reset are optional
	// they help with CLI and debugging, but not required

	// port ops are required to interact with the controller
	if (!hcd->ops->get_port_count) return false;
	if (!hcd->ops->get_port_status) return false;
	if (!hcd->ops->reset_port) return false;
	if (!hcd->ops->enable_port) return false;
	if (!hcd->ops->disable_port) return false;

	// Device creation is very necessary for basic usage
	if (!hcd->ops->device_init) return false;
	if (!hcd->ops->device_destroy) return false;

	// all endpoint ops are required for basic usage
	if (!hcd->ops->endpoint_open) return false;
	if (!hcd->ops->endpoint_close) return false;
	if (!hcd->ops->endpoint_reset) return false;

	// the only transfer explicitly required is execute, async transfers will be important later, but not strictly necessary
	if (!hcd->ops->execute_transfer) return false;

	return true;
}

// the returns from this are never actually shown, should probably do that...
int usb_hcd_register(usb_hcd_t* hcd) {
	if (!hcd || !hcd->ops) {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] usb_hcd_register: invalid HCD or missing ops table\r\n");
		return -1; // TODO: we really need a standardized error return format for the USB stack (and whole OS itself tbh)
	}

	if (!validate_hcd(hcd)) {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] usb_hcd_register: HCD failed validation (missing required ops)\r\n");
		return -1;
	}

	hcd_list_entry_t* entry = (hcd_list_entry_t*) kalloc(sizeof(hcd_list_entry_t));
	if (!entry) {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] usb_hcd_register: failed to allocate hcd_list_entry_t\r\n");
		return -1;
	}


	entry->hcd = hcd;
	entry->next = hcd_list;
	hcd_list = entry;

	// Calls to usb_hcd_register() should only really be done during driver binding.
	// Driver binding expects the binding to "cascade" down the path for all known devices
	// As a result, as we discover (and setup) controllers, we need to keep going down the device path
	return usb_enumerate_hcd(hcd);
}

void usb_hcd_unregister(usb_hcd_t* hcd) {
	// Don't ask about this black magic of a function, I don't remember writing this but it seems to work...
	hcd_list_entry_t** cur = &hcd_list;
	while (*cur) {
		if ((*cur)->hcd == hcd) {
			hcd_list_entry_t* dead = *cur;
			*cur = dead->next;
			kfree(dead);
			return;
		}
		cur = &(*cur)->next;
	}
}

/**
 * @brief Execute a transfer, synchronously (blocking).
 *
 * @param transfer Transfer to execute
 * @return int Return code. 0 if successful, -1 if bad parameter, -2 if failed, others indicate HC problems.
 */
int usb_transfer_sync(usb_transfer_t* transfer) {
	if (!transfer || !transfer->device || !transfer->device->hcd) return -1;

	usb_hcd_t* hcd = transfer->device->hcd;

	int rc = hcd->ops->execute_transfer(hcd, transfer);
	if (rc != 0) {
		return rc;
	}

	return (transfer->status == USB_TRANSFER_COMPLETED) ? 0 : -2;
}

int usb_control_msg(usb_device_t* dev, uint8_t request_type, uint8_t request, uint16_t value, uint16_t index, void* data, uint16_t length, uint32_t timeout_ms) {
	if (!dev || dev->endpoint_count == 0) return -1;

	usb_setup_packet_t setup;
	setup.bmRequestType = request_type;
	setup.bRequest = request;
	setup.wValue = value;
	setup.wIndex = index;
	setup.wLength = length;

	usb_transfer_t transfer;
	memset(&transfer, 0, sizeof(transfer));
	transfer.device = dev;
	transfer.endpoint = &dev->endpoints[0]; /* control transfers always go over ep0 */
	transfer.setup = &setup;
	transfer.buffer = data;
	transfer.length = length;
	transfer.timeout_ms = timeout_ms;

	int rc = usb_transfer_sync(&transfer);
	if (rc != 0) {
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[USB][ERROR] Transfer sync failed with %d\r\n", rc);
		return -1;
	}

	return (int) transfer.actual_length;
}

#include <drivers/usb/usb_class_drivers.h>

void usb_init(void) {

}