#include <stdbool.h>

#include <endian_bits.h>

#include <memory/virtual_mem.h>
#include <memory/kernel_alloc.h>
#include <device/device_manager.h>

#include <drivers/pci.h>
#include <drivers/serial.h>
#include <drivers/usb/hosts/xhci.h>

/**
 * @brief Write a 64-bit xHCI register.
 *
 * According to spec:
 *   - If AC64 = 1, perform a 64-bit write.
 *   - If AC64 = 0, only the lower 32 bits are written, as the upper 32 bits are reserved.
 *
 * @param reg Register address.
 * @param value Value to write.
 * @param ac64 True if the controller supports 64-bit addressing.
 */
static inline void xhci_write_register(volatile void* reg, uint64_t value, bool ac64) {
	if (ac64) {
		mmio_write64(reg, value);
	} else {
		mmio_write32(reg, (uint32_t) value);
	}
}

/**
 * @brief Read a 64-bit xHCI register.
 *
 * According to spec:
 *   - If AC64 = 1, perform a 64-bit read.
 *   - If AC64 = 0, only the lower 32 bits are valid.
 *
 * @param reg Register address.
 * @param ac64 True if the controller supports 64-bit addressing.
 * @return Register value.
 */
static inline uint64_t xhci_read_register(const volatile void* reg, bool ac64) {
	if (ac64) return mmio_read64(reg);
	return (uint64_t) mmio_read32(reg);
}

int xhci_probe(wallos_device_t* dev) {

	// zero indicates that this device does belong to this driver
	// im too lazy to properly check that we can interface with this so I wont right now
	return 0;
}

void xhci_init_regs(xhci_controller_t* hc, uintptr_t base) {
	hc->mmio_base = base;

	hc->cap = (volatile xhci_cap_regs_t*) base;

	hc->op = (volatile xhci_op_regs_t*) (base + hc->cap->caplength);

	hc->runtime = (volatile xhci_runtime_regs_t*) (base + hc->cap->rtsoff);

	hc->doorbell = (volatile xhci_doorbell_regs_t*) (base + hc->cap->dboff);
}

#define XHCI_MMIO_FLAGS  (BIT_PRESENT | BIT_WRITE | BIT_PCD | BIT_SIZE)
void xhci_attach(wallos_device_t* dev) {
	if (!dev) return;

	uint8_t bus = dev->location.pci.bus;
	uint8_t slot = dev->location.pci.slot;
	uint8_t func = dev->location.pci.function;

	int is_io, is_64;
	uintptr_t xhci_base_phys = pci_read_bar(bus, slot, func, 0, &is_io, &is_64);
	if (!xhci_base_phys || is_io) {
		printf_serial("[xHCI] BAR0 is not a valid MMIO BAR\r\n");
		return;
	}

	size_t bar0_size = pci_bar_size(bus, slot, func, 0);
	if (bar0_size == 0) {
		printf_serial("[xHCI][ERROR] bar0 size is impossible\r\n");
		return;
	}

	uintptr_t xhci_base = mapKernelLocationWithFlags(xhci_base_phys, bar0_size, XHCI_MMIO_FLAGS);
	if (!xhci_base) {
		printf_serial("[xHCI][ERROR] failed to map XHCI_BASE phys=0x%lx\r\n", (unsigned long) xhci_base_phys);
		return;
	}

	dev->driver_data = kalloc(sizeof(xhci_controller_t));
	if (dev->driver_data == NULL) {
		printf_serial("[xHCI][ERROR] Failed to allocate driver data.\r\n");
		return;
	}
	xhci_controller_t* hc = (xhci_controller_t*) dev->driver_data;

	uint16_t cmd = pci_config_read16(bus, slot, func, 0x04);
	printf_serial("[xHCI][DEBUG] PCI command=0x%04x\r\n", cmd);
	cmd |= (1 << 1) | (1 << 2);
	pci_config_write16(bus, slot, func, 0x04, cmd);

	volatile uint64_t* p = (volatile uint64_t*) xhci_base;
	printf_serial("[xHCI][DEBUG] qword0=0x%016llx\r\n", mmio_read64(p));

	// This sets all the register fields, including mmio_base
	xhci_init_regs(hc, xhci_base);
	hc->mmio_size = bar0_size;

	uint16_t hc_version = mmio_read16_as32(&hc->cap->hciversion);
	// upper byte has major version, lower byte has minor & patch

	uint8_t major = bcd_to_bin8((hc_version >> 8) & 0xFF);
	uint8_t minor = bcd_to_bin8(hc_version & 0xFF);

	printf_serial("[xHCI] Controller supports xHCI version %d.%d\r\n", major, minor);

	uint32_t hcsparams1 = mmio_read32(&hc->cap->hcsparams1);
	uint8_t max_ports = FIELD_GET(MASK_32_BYTE3, hcsparams1);
	uint32_t max_interrupts = FIELD_GET(GENMASK(18, 8), hcsparams1);
	uint8_t max_dev_slots = FIELD_GET(MASK_32_BYTE0, hcsparams1);

	printf_serial("[xHCI] Controller has %u max ports, %u max interrupters, %u max device slots. \r\n", max_ports, max_interrupts, max_dev_slots);


}

void xhci_detach(wallos_device_t* dev) {

}

#include <drivers/driver_manager.h>

static wallos_driver_t xhci_driver = {
	.name = "xHCI",
	.match_flags = DEV_INT_PCI | DEV_INT_USB | DEV_INT_XHCI,
	.match_mask = DEV_INT_MASK_PROTOCOL | DEV_INT_MASK_CONTROLLER | DEV_INT_MASK_TRANSPORT,

	// vendor id and device id are unused
	.vendor_id = 0,
	.device_id = 0,

	.ops = {
		.attach = xhci_attach,
		.probe = xhci_probe,
		.detach = xhci_detach
	}
};

void xhci_init() {
	dm_register_driver(&xhci_driver);
}