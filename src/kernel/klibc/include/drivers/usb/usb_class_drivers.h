#ifndef WALLOS_USB_CLASS_DRIVERS_H
#define WALLOS_USB_CLASS_DRIVERS_H

#ifdef __cplusplus
extern "C" {
#endif

	 /* Each USB class driver module should have one of these.
	  * It should implement it's own wallos_driver_t and register with the driver manager itself.
	  * Update usb_init() to call these
	  */
	// void usb_hid_driver_init(void);
	// void usb_cdc_driver_init(void);
	// void usb_msc_driver_init(void);

#ifdef __cplusplus
}
#endif
#endif