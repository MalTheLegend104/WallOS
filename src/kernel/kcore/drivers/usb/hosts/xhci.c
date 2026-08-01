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
}

void xhci_parse_hccparams1(uint32_t raw, hccparams1_t* hccp1) {
	hccp1->xhci_extended_cap_ptr = FIELD_GET(MASK_32_UPPER_HALF, raw);

	hccp1->max_psa_size = FIELD_GET(GENMASK(15, 12), raw);

	hccp1->cfc = FIELD_GET(GENMASK(11, 11), raw);
	hccp1->sec = FIELD_GET(GENMASK(10, 10), raw);
	hccp1->spc = FIELD_GET(GENMASK(9, 9), raw);
	hccp1->pae = FIELD_GET(GENMASK(8, 8), raw);
	hccp1->nss = FIELD_GET(GENMASK(7, 7), raw);
	hccp1->ltc = FIELD_GET(GENMASK(6, 6), raw);
	hccp1->lhrc = FIELD_GET(GENMASK(5, 5), raw);
	hccp1->pind = FIELD_GET(GENMASK(4, 4), raw);
	hccp1->ppc = FIELD_GET(GENMASK(3, 3), raw);
	hccp1->csz = FIELD_GET(GENMASK(2, 2), raw);
	hccp1->bnc = FIELD_GET(GENMASK(1, 1), raw);
	hccp1->ac64 = FIELD_GET(GENMASK(0, 0), raw);
}

void xhci_print_hccparams1(const hccparams1_t* hccp1) {
	printf_serial("[xHCI] HCCPARAMS1:\r\n");
	printf_serial("[xHCI]   XECP: 0x%04x\r\n", hccp1->xhci_extended_cap_ptr);
	printf_serial("[xHCI]   MaxPSA Size: %u\r\n", hccp1->max_psa_size);
	printf_serial("[xHCI]   CFC: %u\r\n", hccp1->cfc);
	printf_serial("[xHCI]   SEC: %u\r\n", hccp1->sec);
	printf_serial("[xHCI]   SPC: %u\r\n", hccp1->spc);
	printf_serial("[xHCI]   PAE: %u\r\n", hccp1->pae);
	printf_serial("[xHCI]   NSS: %u\r\n", hccp1->nss);
	printf_serial("[xHCI]   LTC: %u\r\n", hccp1->ltc);
	printf_serial("[xHCI]   LHRC: %u\r\n", hccp1->lhrc);
	printf_serial("[xHCI]   PIND: %u\r\n", hccp1->pind);
	printf_serial("[xHCI]   PPC: %u\r\n", hccp1->ppc);
	printf_serial("[xHCI]   CSZ: %u\r\n", hccp1->csz);
	printf_serial("[xHCI]   BNC: %u\r\n", hccp1->bnc);
	printf_serial("[xHCI]   AC64: %u\r\n", hccp1->ac64);
}

void xhci_parse_hccparams2(uint32_t raw, hccparams2_t* hccp2) {
	hccp2->vtc = FIELD_GET(GENMASK(9, 9), raw);
	hccp2->gsc = FIELD_GET(GENMASK(8, 8), raw);
	hccp2->etc_tsc = FIELD_GET(GENMASK(7, 7), raw);
	hccp2->etc = FIELD_GET(GENMASK(6, 6), raw);
	hccp2->cic = FIELD_GET(GENMASK(5, 5), raw);
	hccp2->lec = FIELD_GET(GENMASK(4, 4), raw);
	hccp2->ctc = FIELD_GET(GENMASK(3, 3), raw);
	hccp2->fsc = FIELD_GET(GENMASK(2, 2), raw);
	hccp2->cmc = FIELD_GET(GENMASK(1, 1), raw);
	hccp2->u3c = FIELD_GET(GENMASK(0, 0), raw);
}

void xhci_print_hccparams2(const hccparams2_t* hccp2) {
	printf_serial("[xHCI] HCCPARAMS2:\r\n");
	printf_serial("[xHCI]   VTC: %u\r\n", hccp2->vtc);
	printf_serial("[xHCI]   GSC: %u\r\n", hccp2->gsc);
	printf_serial("[xHCI]   ETC_TSC: %u\r\n", hccp2->etc_tsc);
	printf_serial("[xHCI]   ETC: %u\r\n", hccp2->etc);
	printf_serial("[xHCI]   CIC: %u\r\n", hccp2->cic);
	printf_serial("[xHCI]   LEC: %u\r\n", hccp2->lec);
	printf_serial("[xHCI]   CTC: %u\r\n", hccp2->ctc);
	printf_serial("[xHCI]   FSC: %u\r\n", hccp2->fsc);
	printf_serial("[xHCI]   CMC: %u\r\n", hccp2->cmc);
	printf_serial("[xHCI]   U3C: %u\r\n", hccp2->u3c);
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

	// This sets all the register fields, including mmio_base
	xhci_init_regs(hc, xhci_base);
	hc->mmio_size = bar0_size;

	/* CAPLENGTH, HCIVERSION */
	// gets caplength and adds it to the mmio_base to get the operations address
	hc->op = (volatile xhci_op_regs_t*) hc->mmio_base + mmio_read8_as32(hc->mmio_base);

	uint16_t hc_version = mmio_read16_as32(&hc->cap->hciversion);
	// upper byte has major version, lower byte has minor & patch

	uint8_t major = bcd_to_bin8((hc_version >> 8) & 0xFF);
	uint8_t minor = bcd_to_bin8(hc_version & 0xFF);

	printf_serial("[xHCI] Controller supports xHCI version %d.%d\r\n", major, minor);

	/* HCSPARAMS1 */
	uint32_t hcsparams1 = mmio_read32(&hc->cap->hcsparams1);
	uint8_t max_ports = FIELD_GET(MASK_32_BYTE3, hcsparams1);
	uint32_t max_interrupts = FIELD_GET(GENMASK(18, 8), hcsparams1);
	uint8_t max_dev_slots = FIELD_GET(MASK_32_BYTE0, hcsparams1);
	printf_serial("[xHCI] Controller has %u max ports, %u max interrupters, %u max device slots. \r\n", max_ports, max_interrupts, max_dev_slots);

	/* HCSPARAMS2 */
	uint32_t hcsparams2 = mmio_read32(&hc->cap->hcsparams2);
	uint16_t max_scratchpad_bufs = /*hi bits*/ FIELD_GET(GENMASK(25, 21), hcsparams2) << 5 | /*low bits*/ FIELD_GET(GENMASK(31, 27), hcsparams2);
	bool spr = FIELD_GET(GENMASK(26, 26), hcsparams2); // This is set to 1 if we are supposed to restore the scratchpad buffer during warm reset. we will 100% ignore this.
	uint8_t erst_max = FIELD_GET(GENMASK(7, 4), hcsparams2) + 1; // this value is raw + 1 according to the spec
	uint16_t ist = FIELD_GET(GENMASK(3, 0), hcsparams2) * 250; // this value is in ns 
	printf_serial("[xHCI] %u max scratchpad bufs, Scratchpad Restore (%s), %u ERST Max, IST %uns\r\n", max_scratchpad_bufs, spr ? "TRUE" : "FALSE", erst_max, ist);

	/* HCSPARAMS3 */
	uint32_t hcsparams3 = mmio_read32(&hc->cap->hcsparams3);
	uint8_t u2_dev_exit_latency = FIELD_GET(MASK_32_BYTE1, hcsparams3);
	uint16_t u1_dev_exit_latency = FIELD_GET(MASK_32_UPPER_HALF, hcsparams3);
	printf_serial("[xHCI] %u U2 Dev Exit Latency, %u U1 Dev Exit Latency. \r\n", u2_dev_exit_latency, u1_dev_exit_latency);

	/* HCCPARAMS1 */
	uint32_t hccparams1_raw = mmio_read32(&hc->cap->hccparams1);
	hccparams1_t hccparams1 = {};
	xhci_parse_hccparams1(hccparams1_raw, &hccparams1);
	xhci_print_hccparams1(&hccparams1);
	hc->ac64 = hccparams1.ac64;
	hc->csz = hccparams1.csz;

	/* DBOFF */
	uint32_t dboff = mmio_read32(&hc->cap->dboff);
	dboff = FIELD_GET(GENMASK(31, 2), dboff);
	printf_serial("[xHCI] DBOFF = 0x%x\r\n", dboff);
	hc->doorbell = (volatile xhci_doorbell_regs_t*) (hc->mmio_base + dboff);

	/* RTSOFF */
	uint32_t rtsoff = mmio_read32(&hc->cap->rtsoff);
	rtsoff = FIELD_GET(GENMASK(31, 5), rtsoff);
	printf_serial("[xHCI] RTSOFF = 0x%x\r\n", rtsoff);
	hc->runtime = (volatile xhci_runtime_regs_t*) (hc->mmio_base + rtsoff);

	/* HCCPARAMS2 */
	uint32_t hccparams2_raw = mmio_read32(&hc->cap->hccparams2);
	hccparams2_t hccparams2 = {};
	xhci_parse_hccparams2(hccparams2_raw, &hccparams2);
	xhci_print_hccparams2(&hccparams2);


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