#include <stdbool.h>
#include <stdio.h>

#include <endian_bits.h>
#include <cpu_io.h>

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

// Oh look it's this stupid function again...
// TODO: we really need to abstract this out, this is x86 dependent
static inline void xhci_delay_us(uint32_t us) {
	for (int i = 0; i < us; i++) {
		outb(0x80, 0); // Takes roughly 1us
	}
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

void xhci_parse_supported_protocol(uintptr_t cap_base, xhci_xec_supported_proto_t* proto) {
	uint32_t dw0 = mmio_read32((volatile void*) (cap_base + 0 * sizeof(uint32_t)));
	uint32_t dw1 = mmio_read32((volatile void*) (cap_base + 1 * sizeof(uint32_t)));
	uint32_t dw2 = mmio_read32((volatile void*) (cap_base + 2 * sizeof(uint32_t)));
	uint32_t dw3 = mmio_read32((volatile void*) (cap_base + 3 * sizeof(uint32_t)));

	/* DWORD 0 */
	proto->rev_minor = bcd_to_bin8(FIELD_GET(GENMASK(23, 16), dw0));
	proto->rev_major = bcd_to_bin8(FIELD_GET(GENMASK(31, 24), dw0));

	/* DWORD 1 */
	proto->name_string[0] = (char) ((dw1 >> 0) & 0xFF);
	proto->name_string[1] = (char) ((dw1 >> 8) & 0xFF);
	proto->name_string[2] = (char) ((dw1 >> 16) & 0xFF);
	proto->name_string[3] = (char) ((dw1 >> 24) & 0xFF);
	proto->name_string[4] = '\0';

	/* DWORD 2 */
	proto->comp_port_offset = FIELD_GET(MASK_32_BYTE0, dw2);
	proto->comp_port_count = FIELD_GET(MASK_32_BYTE1, dw2);
	proto->proto_defined = FIELD_GET(GENMASK(27, 16), dw2);
	proto->psic = FIELD_GET(GENMASK(31, 28), dw2);

	/* DWORD 3 */
	proto->proto_slot_type = FIELD_GET(GENMASK(4, 0), dw3);

	/* Store array count explicitly */
	proto->psi_count = proto->psic;

	/* Parse optional PSI DWORDs (DWORD 4 to 4 + psic - 1) */
	if (proto->psi_count > 0) {
		proto->psi_array = kcalloc(proto->psi_count, sizeof(xhci_xec_supported_proto_psi_t));
		if (!proto->psi_array) {
			proto->psi_count = 0;
			return;
		}

		for (uint8_t i = 0; i < proto->psi_count; i++) {
			volatile void* psi_addr = (volatile void*) (cap_base + (4 + i) * sizeof(uint32_t));
			uint32_t psi_dw = mmio_read32(psi_addr);

			proto->psi_array[i].psiv = FIELD_GET(GENMASK(3, 0), psi_dw);
			proto->psi_array[i].psie = FIELD_GET(GENMASK(5, 4), psi_dw);
			proto->psi_array[i].pfd = FIELD_GET(GENMASK(6, 6), psi_dw);
			proto->psi_array[i].lp = FIELD_GET(GENMASK(15, 14), psi_dw);
			proto->psi_array[i].proto_speed_id_mantissa = FIELD_GET(MASK_32_UPPER_HALF, psi_dw);
		}
	}
}

void xhci_print_supported_protocol(const xhci_xec_supported_proto_t* proto) {
	printf_serial("[xHCI] Supported Protocol:\r\n");
	printf_serial("[xHCI]   Revision: %u.%u\r\n", proto->rev_major, proto->rev_minor);
	printf_serial("[xHCI]   Name: %s\r\n", proto->name_string);
	printf_serial("[xHCI]   Port Offset: %u\r\n", proto->comp_port_offset);
	printf_serial("[xHCI]   Port Count: %u\r\n", proto->comp_port_count);
	printf_serial("[xHCI]   Protocol Defined: 0x%03x\r\n", proto->proto_defined);
	printf_serial("[xHCI]   Slot Type: %u\r\n", proto->proto_slot_type);
	printf_serial("[xHCI]   PSI Count: %u\r\n", proto->psi_count);

	for (uint8_t i = 0; i < proto->psi_count; i++) {
		const xhci_xec_supported_proto_psi_t* psi = &proto->psi_array[i];
		printf_serial("[xHCI]   PSI[%u]: PSIV=%u, PSIE=%u, PFD=%u, LP=%u, Mantissa=%u\r\n",
			i,
			psi->psiv,
			psi->psie,
			psi->pfd,
			psi->lp,
			psi->proto_speed_id_mantissa);
	}
}

void xhci_parse_legacy_support(uintptr_t cap_base, xhci_xec_legacy_support_t* legsup) {
	uint32_t dw0 = mmio_read32((volatile void*) (cap_base + 0 * sizeof(uint32_t)));
	uint32_t dw1 = mmio_read32((volatile void*) (cap_base + 1 * sizeof(uint32_t)));

	/* DWORD 0: USBLEGSUP Parsing */
	legsup->cap_id = FIELD_GET(GENMASK(7, 0), dw0);
	legsup->next_cap_ptr = FIELD_GET(GENMASK(15, 8), dw0);
	legsup->hc_bios_owned = FIELD_GET(GENMASK(16, 16), dw0);
	legsup->hc_os_owned = FIELD_GET(GENMASK(24, 24), dw0);

	/* DWORD 1: USBLEGCTLSTS Parsing */
	legsup->usb_smi_enable = FIELD_GET(GENMASK(0, 0), dw1);
	legsup->smi_on_host_sys_err_enable = FIELD_GET(GENMASK(1, 1), dw1);
	legsup->smi_on_os_ownership_enable = FIELD_GET(GENMASK(2, 2), dw1);
	legsup->smi_on_pci_command_enable = FIELD_GET(GENMASK(3, 3), dw1);
	legsup->smi_on_bar_enable = FIELD_GET(GENMASK(4, 4), dw1);
	legsup->smi_on_event_int_enable = FIELD_GET(GENMASK(13, 13), dw1);

	legsup->smi_on_host_sys_err = FIELD_GET(GENMASK(16, 16), dw1);
	legsup->smi_on_os_ownership_change = FIELD_GET(GENMASK(17, 17), dw1);
	legsup->smi_on_pci_command = FIELD_GET(GENMASK(18, 18), dw1);
	legsup->smi_on_bar = FIELD_GET(GENMASK(19, 19), dw1);
	legsup->smi_on_event_int = FIELD_GET(GENMASK(29, 29), dw1);
}

void xhci_print_legacy_support(const xhci_xec_legacy_support_t* legsup) {
	printf_serial("[xHCI] Legacy Support:\r\n");
	printf_serial("[xHCI]   Cap ID: %u\r\n", legsup->cap_id);
	printf_serial("[xHCI]   Next Cap: %u\r\n", legsup->next_cap_ptr);
	printf_serial("[xHCI]   HC BIOS Owned: %u\r\n", legsup->hc_bios_owned);
	printf_serial("[xHCI]   HC OS Owned: %u\r\n", legsup->hc_os_owned);

	printf_serial("[xHCI]   USB SMI Enable: %u\r\n", legsup->usb_smi_enable);
	printf_serial("[xHCI]   SMI HSE Enable: %u\r\n", legsup->smi_on_host_sys_err_enable);
	printf_serial("[xHCI]   SMI OS Enable: %u\r\n", legsup->smi_on_os_ownership_enable);
	printf_serial("[xHCI]   SMI PCI Enable: %u\r\n", legsup->smi_on_pci_command_enable);
	printf_serial("[xHCI]   SMI BAR Enable: %u\r\n", legsup->smi_on_bar_enable);
	printf_serial("[xHCI]   SMI Event Enable: %u\r\n", legsup->smi_on_event_int_enable);

	printf_serial("[xHCI]   SMI HSE Status: %u\r\n", legsup->smi_on_host_sys_err);
	printf_serial("[xHCI]   SMI OS Status: %u\r\n", legsup->smi_on_os_ownership_change);
	printf_serial("[xHCI]   SMI PCI Status: %u\r\n", legsup->smi_on_pci_command);
	printf_serial("[xHCI]   SMI BAR Status: %u\r\n", legsup->smi_on_bar);
	printf_serial("[xHCI]   SMI Event Status: %u\r\n", legsup->smi_on_event_int);
}

xhci_xec_capability_id_t get_id_from_value(uint8_t value) {
	switch (value) {
		case 1:    return XEC_USB_LEGACY;
		case 2:    return XEC_SUPPORTED_PROTO;
		case 3:    return XEC_EXT_POWER_MANAGEMENT;
		case 4:    return XEC_IO_VIRT;
		case 5:    return XEC_MESSAGE_INTERRUPT;
		case 6:    return XEC_LOCAL_MEMORY;
		case 10:   return XEC_USB_DEBUG;
		case 17:   return XEC_EXT_MESSAGE_INTERRUPT;
		case 192 ... 255: return XEC_VENDOR_DEFINED; // unfortunately need this to use the GCC ... range extension to make this readable
		default: return XEC_RESERVED;
	}
}

void xhci_bios_handoff(uintptr_t cap_base, xhci_xec_legacy_support_t* legsup) {
	if (!cap_base) return;

	const uint32_t XHCI_LEGSUP_BIOS_OWNED = BIT(16);
	const uint32_t XHCI_LEGSUP_OS_OWNED = BIT(24);
	const uint32_t XHCI_LEGCTLSTS_SMI_ENABLES = (GENMASK(4, 0) | BIT(13));
	const uint32_t XHCI_LEGCTLSTS_SMI_STATUSES = (GENMASK(19, 16) | BIT(29));

	uint32_t dw0 = mmio_read32((volatile void*) cap_base);

	// If BIOS owns it, we must negotiate handoff
	if (dw0 & XHCI_LEGSUP_BIOS_OWNED) {
		printf_serial("[xHCI] BIOS owns controller. Initiating handoff...\r\n");
		printf_color(PRINT_COLOR_PURPLE, PRINT_DEFAULT_BG, "[xHCI] BIOS owns controller. Initiating handoff...\n");

		// Set OS Owned bit
		BIT_SET(dw0, 24);
		mmio_write32((volatile void*) cap_base, dw0);

		// Wait for BIOS to clear its ownership bit.
		// The xHCI spec recommends waiting up to 1 second.
		bool handoff_successful = false;
		for (int i = 0; i < 100; i++) { // Loop 100 times, 10ms each = 1 second
			dw0 = mmio_read32((volatile void*) cap_base);

			if (!(dw0 & XHCI_LEGSUP_BIOS_OWNED) && (dw0 & XHCI_LEGSUP_OS_OWNED)) {
				handoff_successful = true;
				break;
			}

			xhci_delay_us(10000);
		}

		if (!handoff_successful) {
			printf_serial("[xHCI][WARN] BIOS handoff timed out! Forcing ownership.\r\n");
			printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "[xHCI][WARN] BIOS handoff timed out! Forcing ownership.\r\n");
			// There's a chance the BIOS will never respond.
			// We proceed anyway, relying on OS_OWNED being set.
		} else {
			printf_serial("[xHCI] BIOS handoff successful.\r\n");
			printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "[xHCI] BIOS handoff successful.\r\n");
		}
	} else {
		printf_serial("[xHCI] BIOS does not own controller. Claiming OS ownership.\r\n");
		printf_color(PRINT_COLOR_PURPLE, PRINT_DEFAULT_BG, "[xHCI] BIOS does not own controller. Claiming OS ownership.\r\n");
		BIT_SET(dw0, 24);
		mmio_write32((volatile void*) cap_base, dw0);
	}

	uint32_t dw1 = mmio_read32((volatile void*) (cap_base + 4));

	// Clear all SMI Enable bits to 0
	MASK_CLEAR(dw1, XHCI_LEGCTLSTS_SMI_ENABLES);

	// Clear all SMI Status bits to 0 by writing 1 (W1C bits)
	MASK_SET(dw1, XHCI_LEGCTLSTS_SMI_STATUSES);

	mmio_write32((volatile void*) (cap_base + 4), dw1);

	// Forcefully update the struct
	if (legsup) {
		xhci_parse_legacy_support(cap_base, legsup);
	}
}

#define XHCI_MMIO_FLAGS  (BIT_PRESENT | BIT_WRITE | BIT_PCD | BIT_SIZE)
void xhci_attach(wallos_device_t* dev) {
	if (!dev) return;

	printf_serial("[xHCI] Attaching device %s...\r\n", dev->name);
	printf_color(PRINT_COLOR_PURPLE, PRINT_DEFAULT_BG, "[xHCI] Attaching device %s...\n", dev->name);

	uint8_t bus = dev->location.pci.bus;
	uint8_t slot = dev->location.pci.slot;
	uint8_t func = dev->location.pci.function;

	int is_io, is_64;
	uintptr_t xhci_base_phys = pci_read_bar(bus, slot, func, 0, &is_io, &is_64);
	if (!xhci_base_phys || is_io) {
		printf_serial("[xHCI][ERROR] BAR0 is not a valid MMIO BAR\r\n");
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[xHCI][ERROR] BAR0 is not a valid MMIO BAR\n");
		return;
	}

	size_t bar0_size = pci_bar_size(bus, slot, func, 0);
	if (bar0_size == 0) {
		printf_serial("[xHCI][ERROR] bar0 size is impossible\r\n");
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[xHCI][ERROR] bar0 size is impossible\n");
		return;
	}

	uintptr_t xhci_base = mapKernelLocationWithFlags(xhci_base_phys, bar0_size, XHCI_MMIO_FLAGS);
	if (!xhci_base) {
		printf_serial("[xHCI][ERROR] failed to map XHCI_BASE phys=0x%llx\r\n", (unsigned long) xhci_base_phys);
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[xHCI][ERROR] failed to map XHCI_BASE phys=0x%llx\n", (unsigned long) xhci_base_phys);
		return;
	}

	dev->driver_data = kalloc(sizeof(xhci_controller_t));
	if (dev->driver_data == NULL) {
		printf_serial("[xHCI][ERROR] Failed to allocate driver data.\r\n");
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[xHCI][ERROR] Failed to allocate driver data.\n");
		return;
	}
	xhci_controller_t* hc = (xhci_controller_t*) dev->driver_data;

	// This sets all the register fields, including mmio_base
	xhci_init_regs(hc, xhci_base);
	hc->mmio_size = bar0_size;

	/* CAPLENGTH, HCIVERSION */
	// gets caplength and adds it to the mmio_base to get the operations address
	hc->op = (volatile xhci_op_regs_t*) hc->mmio_base + mmio_read8_as32((volatile void*) hc->mmio_base);

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
	printf_serial("[xHCI] Controller has %u max ports, %u max interrupters, %u max device slots.\r\n", max_ports, max_interrupts, max_dev_slots);
	printf_color(PRINT_COLOR_PURPLE, PRINT_DEFAULT_BG, "[xHCI] Controller has %u max ports, %u max device slots.\n", max_ports, max_dev_slots);

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


	if (hccparams1.xhci_extended_cap_ptr > 0) {
		uintptr_t xecp = hc->mmio_base + (hccparams1.xhci_extended_cap_ptr << 2);

		// Initial discovery, we simply populate the list for our own sanity, we will parse specific types later.
		uint8_t index = 0;
		while (true) {
			uint32_t current_xce = mmio_read32((volatile void*) xecp);

			uint8_t capability = FIELD_GET(MASK_32_BYTE0, current_xce);
			uint8_t next_xce = FIELD_GET(MASK_32_BYTE1, current_xce);
			uint16_t capability_specific = FIELD_GET(MASK_32_UPPER_HALF, current_xce);

			printf_serial("[xHCI] XCEP[%u] has cap=%u, offset to next = %u, cap_spec = %u\r\n", index, capability, next_xce, capability_specific);

			// TODO: This list needs to be freed in detach
			xhci_extended_compat_t* xec_struct = (xhci_extended_compat_t*) kcalloc(1, sizeof(xhci_extended_compat_t));
			xec_struct->addr = (void*) xecp;
			xec_struct->capability = get_id_from_value(capability);
			xec_struct->next_node = NULL;
			xec_struct->next_xec_addr = NULL;

			if (hc->first_xce == NULL) {
				hc->first_xce = xec_struct;
				hc->last_xce = xec_struct;
			} else {
				hc->last_xce->next_node = xec_struct;
				hc->last_xce = xec_struct;
			}

			if (next_xce == 0) break;

			index++;
			xecp = xecp + (next_xce << 2);
			xec_struct->next_xec_addr = (void*) xecp;
		}

		xhci_extended_compat_t* list = hc->first_xce;
		while (list != NULL) {
			switch (list->capability) {
				case XEC_USB_LEGACY: {
						xhci_xec_legacy_support_t* legacy_support = (xhci_xec_legacy_support_t*) kcalloc(1, sizeof(xhci_xec_legacy_support_t));
						if (!legacy_support) {
							return; // TODO: should probably cleanup shit
						}
						list->specific_data = legacy_support;
						xhci_bios_handoff((uintptr_t) list->addr, legacy_support); // bios handoff deals with populating legacy_support
						xhci_print_legacy_support(legacy_support);
						break;
					}
				case XEC_SUPPORTED_PROTO: {
						xhci_xec_supported_proto_t* supported_proto = (xhci_xec_supported_proto_t*) kcalloc(1, sizeof(xhci_xec_supported_proto_t));
						list->specific_data = (void*) supported_proto;
						xhci_parse_supported_protocol((uintptr_t) list->addr, supported_proto);
						xhci_print_supported_protocol(supported_proto);
						break;
					}
				case XEC_EXT_POWER_MANAGEMENT:
				case XEC_IO_VIRT:
				case XEC_MESSAGE_INTERRUPT:
				case XEC_LOCAL_MEMORY:
				case XEC_USB_DEBUG:
				case XEC_EXT_MESSAGE_INTERRUPT:
				default: break;
			}
			list = list->next_node;
		}

	}
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