#ifndef WALLOS_DEVICE_MANAGER_H
#define WALLOS_DEVICE_MANAGER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Device Interface
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

	typedef uint64_t device_interface_flags_t;
	/*
	 * |63|62|61|60      53|52                32|31                12|11         0|
	 *  0  0  0  0000000000 00000000000000000000 00000000000000000000 000000000000
	 * Meaning of Bits:
	 *  0-11: Physical Transport Type. What physical communication type is used (MMIO, PCI, USB, etc.).
	 * 12-31: Controller Type. What interface is used to control the device (AHCI, NVMe, XHCI, etc.).
	 * 32-52: Protocol descriptors. What protocol(s) the device follows/exposes (HID, mass storage, network, etc.).
	 * 53-60: Embedded device types. This is for low level communication types (GPIO, UART, SPI).
	 *    61: Already Bound Device. Flag for devices fully initialized and managed by their driver early on.
	 *    62: Indicates that the "device" is an interface only. This is meant to isolate the parent root devices from actual devices. (/dev/serial, /dev/pci, etc.)
	 *    63: Indicates an unknown device. This *can* still be defined with other flags.
	 *
	 * A value of all ones (0xFFFFFFFFFFFFFFFF) indicates an invalid device.
	 */
	typedef enum {
		DEV_INT_NONE = 0,

		/* Physical / Transport (Bits 0-11) */
		DEV_INT_MMIO = (1ULL << 0),  // Memory-Mapped I/O
		DEV_INT_PORT_IO = (1ULL << 1),  // Port-Mapped I/O 
		DEV_INT_PCI = (1ULL << 2),  // PCI/PCIe bus, they share the same interface so we don't differentiate between them
		DEV_INT_USB = (1ULL << 3),  // USB bus 
		DEV_INT_PLATFORM = (1ULL << 4),  // Firmware-described devices (ACPI)
		DEV_INT_VIRTIO = (1ULL << 5),  // VirtIO transport
		DEV_INT_TIMER = (1ULL << 6), // Generic timer interface

		/* Controller (Bits 12-31) */
		DEV_INT_AHCI = (1ULL << 12), // AHCI SATA 
		DEV_INT_NVME = (1ULL << 13), // NVMe
		DEV_INT_XHCI = (1ULL << 14), // eXtensible Host Controller Interface (USB 3.x controller)
		DEV_INT_EHCI = (1ULL << 15), // Enhanced Host Controller Interface (USB 2.0 controller)
		DEV_INT_OHCI = (1ULL << 16), // Open Host Controller Interface (USB 1.1 controller, non-Intel)
		DEV_INT_UHCI = (1ULL << 17), // Universal Host Controller Interface (USB 1.1 controller, Intel)
		DEV_INT_HDA = (1ULL << 18), // High Definition Audio

		/* Protocol / Class (Bits 32-51) */
		DEV_INT_HID = (1ULL << 32), // Human Interface Device
		DEV_INT_MSC = (1ULL << 33), // Mass Storage Class (USB storage devices using SCSI-like commands)
		DEV_INT_CDC = (1ULL << 34), // Communications Device Class (USB serial, modems, network adapters)
		DEV_INT_AUDIO = (1ULL << 35), // Audio device class
		DEV_INT_NET_MAC = (1ULL << 36), // Network MAC layer (Ethernet/Wi-Fi link-layer interface)
		DEV_INT_VIDEO = (1ULL << 37), // Video/display interface

		/* Low-Speed / Embedded (Bits 52-60) */
		DEV_INT_I2C = (1ULL << 52), // Inter-Integrated Circuit bus
		DEV_INT_SPI = (1ULL << 53), // Serial Peripheral Interface bus
		DEV_INT_UART = (1ULL << 54), // Universal Asynchronous Receiver/Transmitter 
		DEV_INT_GPIO = (1ULL << 55), // General Purpose I/O
		DEV_INT_SDIO = (1ULL << 56), // SD I/O interface (SD cards and embedded peripherals)

		/* Bit 61: Already Bound Device
		 * The device was bound to the driver during initialization.
		 * This is for things like timers that are discovered, initialized, and handled all by their own driver during init.
		 * Things that are bound by dm_bind() DO NOT get this flag set.
		 * This is merely a flag to tell the driver system "hey we already got this covered"
		 */
		DEV_INT_ALREADY_BOUND = (1ULL << 61),

		/* Bit 62: Synthetic node
		 * Example: the "pci" node created by pci_discover to parent all PCI devices.
		 * DEV_INT_IS_REAL(x) can be used to skip these during probe/driver binding.
		 */
		DEV_INT_INTERFACE_ONLY = (1ULL << 62),

		// Invalid/Unknown device
		DEV_INT_UNKNOWN = (1ULL << 63),
		DEV_INT_INVALID = 0xFFFFFFFFFFFFFFFF
	} device_interface_t;


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
	/**
	 * @brief Get the flag name in string form.
	 *
	 * @param flag Flag you want the string for.
	 * @return const char* String containing the name
	 */
	static const char* get_flag_name(device_interface_t flag) {
		switch (flag) {
			/* Physical / Transport */
			case DEV_INT_MMIO: return "mmio";
			case DEV_INT_PORT_IO: return "port_io";
			case DEV_INT_PCI: return "pci";
			case DEV_INT_USB: return "usb";
			case DEV_INT_PLATFORM: return "platform";
			case DEV_INT_VIRTIO: return "virtio";
			case DEV_INT_TIMER: return "timer";

			/* Controller */
			case DEV_INT_AHCI: return "ahci";
			case DEV_INT_NVME: return "nvme";
			case DEV_INT_XHCI: return "xhci";
			case DEV_INT_EHCI: return "ehci";
			case DEV_INT_OHCI: return "ohci";
			case DEV_INT_UHCI: return "uhci";
			case DEV_INT_HDA: return "hda";

			/* Protocol / Class */
			case DEV_INT_HID: return "hid";
			case DEV_INT_MSC: return "msc";
			case DEV_INT_CDC: return "cdc";
			case DEV_INT_AUDIO: return "audio";
			case DEV_INT_NET_MAC: return "net_mac";
			case DEV_INT_VIDEO: return "video";

			/* Low-Speed / Embedded */
			case DEV_INT_I2C: return "i2c";
			case DEV_INT_SPI: return "spi";
			case DEV_INT_UART: return "uart";
			case DEV_INT_GPIO: return "gpio";
			case DEV_INT_SDIO: return "sdio";

			/* Special */
			case DEV_INT_ALREADY_BOUND: return "already bound";
			case DEV_INT_INTERFACE_ONLY: return "interface";
			case DEV_INT_UNKNOWN: return "unknown";

			default: return NULL;
		}
	}
#pragma GCC diagnostic pop

// ------------------------------------------------------------------------------------------------
// Helper Macros
// ------------------------------------------------------------------------------------------------
#define DEV_INT_MASK_TRANSPORT   0x0000000000000FFFULL // Bits 0-11
#define DEV_INT_MASK_CONTROLLER  0x00000000FFFFF000ULL // Bits 12-31
#define DEV_INT_MASK_PROTOCOL    0x000FFFFF00000000ULL // Bits 32-51
#define DEV_INT_MASK_EMBEDDED    0x1FF0000000000000ULL // Bits 52-60
#define DEV_INT_MASK_UNKNOWN     0x8000000000000000ULL // Bit 63

#define DEV_INT_GET_TRANSPORT(x)   ((x) & DEV_INT_MASK_TRANSPORT)
#define DEV_INT_GET_CONTROLLER(x)  ((x) & DEV_INT_MASK_CONTROLLER)
#define DEV_INT_GET_PROTOCOL(x)    ((x) & DEV_INT_MASK_PROTOCOL)
#define DEV_INT_GET_EMBEDDED(x)    ((x) & DEV_INT_MASK_EMBEDDED)
#define DEV_INT_IS_UNKNOWN(x)      (((x) & DEV_INT_MASK_UNKNOWN) != 0)

#define DEV_INT_HAS(x, flag)       (((x) & (flag)) != 0)
#define DEV_INT_HAS_ALL(x, flags)  (((x) & (flags)) == (flags))
#define DEV_INT_HAS_ANY(x, flags)  (((x) & (flags)) != 0)

#define DEV_INT_HAS_TRANSPORT(x)   (DEV_INT_GET_TRANSPORT(x) != 0)
#define DEV_INT_HAS_CONTROLLER(x)  (DEV_INT_GET_CONTROLLER(x) != 0)
#define DEV_INT_HAS_PROTOCOL(x)    (DEV_INT_GET_PROTOCOL(x) != 0)
#define DEV_INT_HAS_EMBEDDED(x)    (DEV_INT_GET_EMBEDDED(x) != 0)

#define DEV_INT_IS_PCI(x)             DEV_INT_HAS(x, DEV_INT_PCI)
#define DEV_INT_IS_USB(x)             DEV_INT_HAS(x, DEV_INT_USB)
#define DEV_INT_IS_MMIO(x)            DEV_INT_HAS(x, DEV_INT_MMIO)
#define DEV_INT_IS_STORAGE_CTRL(x)    DEV_INT_HAS_ANY((x), DEV_INT_AHCI | DEV_INT_NVME)
#define DEV_INT_IS_USB_CTRL(x)        DEV_INT_HAS_ANY((x), DEV_INT_XHCI | DEV_INT_EHCI | DEV_INT_OHCI | DEV_INT_UHCI)
#define DEV_INT_IS_NONE(x)            ((x) == DEV_INT_NONE)
#define DEV_INT_IS_INVALID(x)         ((x) == DEV_INT_INVALID)
#define DEV_INT_IS_INTERFACE_ONLY(x)  DEV_INT_HAS(x, DEV_INT_INTERFACE_ONLY)
#define DEV_INT_IS_ALREADY_BOUND(x)   DEV_INT_HAS(x, DEV_INT_ALREADY_BOUND)
#define DEV_INT_IS_REAL(x)            (!DEV_INT_IS_INTERFACE_ONLY(x))

#define DEV_INT_MATCH_CONTROLLER(x, flag) (DEV_INT_GET_CONTROLLER(x) == (flag))
#define DEV_INT_MATCH_TRANSPORT(x, flag)  (DEV_INT_GET_TRANSPORT(x) == (flag))

#define DEV_INT_SET(x, flag)      ((x) |= (flag))
#define DEV_INT_CLEAR(x, flag)    ((x) &= ~(flag))
#define DEV_INT_TOGGLE(x, flag)   ((x) ^= (flag))

#define DEV_INT_MARK_UNKNOWN(x)   ((x) |= DEV_INT_UNKNOWN)
#define DEV_INT_CLEAR_UNKNOWN(x)  ((x) &= ~DEV_INT_UNKNOWN)

#define DEV_INT_MARK_BOUND(x)     ((x) |= DEV_INT_ALREADY_BOUND)
#define DEV_INT_CLEAR_BOUND(x)    ((x) &= ~DEV_INT_ALREADY_BOUND)
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Device Descriptor
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
	// Forward declare needed for the device struct to be able to track it's bound driver.
	typedef struct wallos_driver wallos_driver_t;

	typedef struct wallos_device {
		device_interface_flags_t interfaces;

		// This is managed by the driver/owner of this struct.
		// This MUST outlive the device struct itself.
		// It should be freed/cleaned up AFTER remove_device() is called.
		const char* name;

		// This is managed by the device manager itself.
		// Nothing should modify this directly. 
		// Call recalculate_device_path() if it must be reconstructed.
		char* path;

		// Identity Information
		uint16_t vendor_id;
		uint16_t device_id;
		uint16_t subsystem_id;

		// Topology
		// Used to see what devices live under this, and what lives above it
		// This can be used to walk both up and down the tree
		struct wallos_device* parent;
		struct wallos_device* first_child;   // head of this device's child list
		struct wallos_device* next_sibling;  // next child in parent's list
		// This is traversed using HEAD->first_child->next_sibling->next_sibling->...->NULL

		// Bus-Specific Location Data
		union {
			struct {
				uint8_t bus;
				uint8_t slot;
				uint8_t function;
			} pci;
			struct {
				uint8_t port;
				uint8_t address;
			} usb;
			struct {
				uintptr_t base_address;
				uint32_t  irq;
			} mmio;
		} location;

		void* driver_data; // Opaque pointer for the bound driver's private state
		wallos_driver_t* bound_driver;
	} wallos_device_t;

// Traverse all direct children of a device
// This IS NOT recursive
#define DEV_FOR_EACH_CHILD(parent, child) for (wallos_device_t* (child) = (parent)->first_child; (child) != NULL; (child) = (child)->next_sibling)

// Check if a device has any children
#define DEV_HAS_CHILDREN(dev) ((dev)->first_child != NULL)


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Device Manager Registry Functions
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

	wallos_device_t* create_device(device_interface_flags_t flags, const char* name);
	void register_device(wallos_device_t* dev);
	void remove_device(wallos_device_t* dev);
	void recalculate_device_path(wallos_device_t* dev);

	/**
	 * @brief Get a string representation of the path to the device in the device tree.
	 *
	 * The caller MUST CALL `kfree()` ON THIS.
	 *
	 * @param dev Pointer to the device you wish to get the path to.
	 * @return char* A *heap allocated* string. YOU MUST CALL `kfree()` ON THIS.
	 */
	char* get_device_path(wallos_device_t* dev);

	/**
	 * @brief Print the entire device tree relating to the device.
	 * This includes all children, their children, ... recursively.
	 * It does not print the devices parent.
	 *
	 * Pass NULL to print the *entire* device tree registry.
	 *
	 * @param dev Device to list the tree from. NULL to
	 * @return * void
	 */
	// void print_device_tree(wallos_device_t* dev);

	wallos_device_t* find_device_by_name(const char* name);
	wallos_device_t* find_device_by_path(const char* path);
	wallos_device_t* resolve_device(const char* input);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
	// idk why but GCC complains about this being unused, when it very much is used
	static const char* dev_aliases[] = { "dev" };
#pragma GCC diagnostic pop
	int device_cmd(int argc, char** argv);


	/**
	 * @brief Used internally by the device manager system to track all devices in a linked list.
	 * This is meant to only be used internally, with a few other subsystems needing to touch the device registry.
	 * If you're using this, be ABSOLUTELY sure it's the correct way to do it.
	 */
	typedef struct device_node {
		wallos_device_t* dev;
		struct device_node* next;
	} device_node_t;

	/**
	 * @brief Get the internal device registry.
	 * This should be used VERY carefully. There almost always a better way to access devices than this.
	 * This should only be used when *every* device needs to be iterated over.
	 *
	 * @return device_node_t* Root node of the device registry.
	 */
	device_node_t* internal_get_dev_registry();
#ifdef __cplusplus
}
#endif
#endif // WALLOS_DEVICE_MANAGER_H