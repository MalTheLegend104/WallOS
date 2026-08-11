#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <endian_bits.h>
#include <cpu_io.h>

#include <memory/virtual_mem.h>
#include <memory/kernel_alloc.h>
#include <device/device_manager.h>

#include <drivers/pci.h>
#include <drivers/serial.h>
#include <drivers/usb/hosts/xhci.h>
#include <drivers/usb/usb_core.h>

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

/**
 * @brief Ensures previous memory operations complete before continuing.
 *
 */
static inline void xhci_memory_fence() {
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
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

void xhci_print_port_protocols(xhci_controller_t* hc) {
	if (!hc || !hc->first_xce) return;

	xhci_extended_compat_t* current = hc->first_xce;
	while (current != NULL) {
		if (current->capability == XEC_SUPPORTED_PROTO && current->specific_data != NULL) {
			xhci_xec_supported_proto_t* proto = (xhci_xec_supported_proto_t*) current->specific_data;

			if (proto->comp_port_count > 0) {
				uint8_t start_port = proto->comp_port_offset;
				uint8_t end_port = start_port + proto->comp_port_count - 1;

				if (start_port == end_port) {
					printf_serial("[xHCI] Port %u is USB %u.%u\r\n", start_port, proto->rev_major, proto->rev_minor);
					printf_color(PRINT_COLOR_PURPLE, PRINT_DEFAULT_BG, "[xHCI] Port %u is USB %u.%u\n", start_port, proto->rev_major, proto->rev_minor);
				} else {
					printf_serial("[xHCI] Ports %u-%u are USB %u.%u\r\n", start_port, end_port, proto->rev_major, proto->rev_minor);
					printf_color(PRINT_COLOR_PURPLE, PRINT_DEFAULT_BG, "[xHCI] Ports %u-%u are USB %u.%u\n", start_port, end_port, proto->rev_major, proto->rev_minor);
				}
			} else {
				printf_serial("[xHCI] No ports assigned to USB %u.%u\r\n", proto->rev_major, proto->rev_minor);
				printf_color(PRINT_COLOR_PURPLE, PRINT_DEFAULT_BG, "[xHCI] No ports assigned to USB %u.%u\r\n", proto->rev_major, proto->rev_minor);
			}
		}
		current = current->next_node;
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

void xhci_reset_controller(xhci_controller_t* hc) {
	printf_serial("[xHCI] Resetting controller... ");
	printf_color(PRINT_COLOR_PURPLE, PRINT_DEFAULT_BG, "[xHCI] Resetting controller...");

	const uint32_t XHCI_USBCMD_HCRST = BIT(1);  // Host Controller Reset
	const uint32_t XHCI_USBSTS_HCH = BIT(0);  // HC Halted
	const uint32_t XHCI_USBSTS_CNR = BIT(11); // Controller Not Ready

	// printf_serial("\r\nhc->cap = 0x%llx, hc->cap->caplength = 0x%llx, hc->op = 0x%llx, hc->op->usbcmd = 0x%llx\r\n", hc->cap, FIELD_GET(MASK_32_BYTE0, mmio_read32(&hc->cap->caplength)), hc->op, &hc->op->usbcmd);

	// Clear Run/Stop bit
	uint32_t usbcmd = mmio_read32(&hc->op->usbcmd);
	BIT_CLEAR(usbcmd, 0); // BIT0 is Run/Stop
	mmio_write32(&hc->op->usbcmd, usbcmd);

	// Wait for HC to halt
	while ((mmio_read32(&hc->op->usbsts) & XHCI_USBSTS_HCH) == 0) {
		xhci_delay_us(1000);
	}

	printf_serial("controller halted. Issuing reset...\r\n");

	// Issue Reset
	usbcmd = mmio_read32(&hc->op->usbcmd);
	BIT_SET(usbcmd, 1);
	mmio_write32(&hc->op->usbcmd, usbcmd);

	// Wait for reset to complete (HCRST clears to 0)
	while ((mmio_read32(&hc->op->usbcmd) & XHCI_USBCMD_HCRST) != 0) {
		xhci_delay_us(1000);
	}

	// Wait for CNR to clear
	while ((mmio_read32(&hc->op->usbsts) & XHCI_USBSTS_CNR) != 0) {
		xhci_delay_us(1000);
	}

	printf_serial("Controller successfully reset\r\n");
	printf_color(PRINT_COLOR_PURPLE, PRINT_DEFAULT_BG, " done\n");
}

bool xhci_ring_enqueue(xhci_ring_t* ring, const trb_t* trb) {
	if (!ring || !trb) return false;

	trb_t* dst = &ring->trbs[ring->enqueue];

	/* Copy the TRB without its cycle bit. */
	*dst = *trb;
	FIELD_WRITE(dst->control, BIT(0), 0);

	xhci_memory_fence();

	FIELD_WRITE(dst->control, BIT(0), ring->cycle);

	ring->enqueue++;

	/* Link TLB */
	if (ring->enqueue == ring->trb_count - 1) {
		/* Skip over the Link TRB. The hardware consumes it, not us.
		 * Since the Link TRB has TC=1, we also toggle our producer cycle state.
		 */
		ring->enqueue = 0;
		ring->cycle = !ring->cycle;
	}

	return true;
}

void xhci_write_max_slots(xhci_controller_t* hc, uint8_t slots) {
	if (!hc || slots == 0) return;

	uint32_t config_reg = mmio_read32(&hc->op->config);
	FIELD_WRITE(config_reg, MASK_32_BYTE0, slots);
	mmio_write32(&hc->op->config, config_reg);
}

// Computes the physical address of the interrupter's current dequeue TRB.
static inline uintptr_t xhci_interrupter_dequeue_phys(xhci_interrupter_t* ir) {
	xhci_ring_t* seg = &ir->segments[ir->dequeue_segment];
	return seg->trbs_phys + (ir->dequeue * sizeof(trb_t));
}

// Advances the interrupter's dequeue pointer by one TRB, wrapping to next segment as needed
// CCS only toggles when we wrap back around to segment 0
void xhci_interrupter_advance_dequeue(xhci_interrupter_t* ir) {
	ir->dequeue++;
	if (ir->dequeue >= ir->segments[ir->dequeue_segment].trb_count) {
		ir->dequeue = 0;
		ir->dequeue_segment++;
		if (ir->dequeue_segment >= ir->erst_size) {
			ir->dequeue_segment = 0;
			ir->cycle = !ir->cycle;
		}
	}
}

// Writes the current dequeue pointer + DESI to ERDP and clears EHB so the controller is free to signal another interrupt for this interrupter.
// Called once at init (dequeue at segment 0, TRB 0) and again after each batch of events software has finished draining.
void xhci_interrupter_update_erdp(xhci_controller_t* hc, uint16_t index) {
	xhci_interrupter_t* ir = &hc->interrupters[index];

	uint64_t erdp_value = xhci_interrupter_dequeue_phys(ir) & GENMASK(63, 4);
	FIELD_WRITE(erdp_value, GENMASK(2, 0), ir->dequeue_segment); // DESI
	FIELD_WRITE(erdp_value, BIT(3), 1); // EHB - RW1C, always write 1 to clear

	xhci_memory_fence();
	xhci_write_register(&hc->runtime->ir[index].erdp, erdp_value, hc->ac64);
}

bool xhci_init_interrupter(xhci_controller_t* hc, uint16_t index) {
	xhci_interrupter_t* ir = &hc->interrupters[index];

	ir->erst_size = XHCI_ERST_SEGMENTS_PER_INTERRUPTER;

	ir->segments = kcalloc(ir->erst_size, sizeof(xhci_ring_t));
	if (!ir->segments) {
		printf_serial("[xHCI][ERROR] Failed to allocate segment array for interrupter %u...\r\n", index);
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[xHCI][ERROR] Failed to allocate segment array for interrupter %u...\n", index);
		return false;
	}

	ir->erst = kalloc_dma(sizeof(xhci_event_segment_table_t) * ir->erst_size, DMA_ALIGN_64 | DMA_ZONE_ANY, PDE_FLAGS_UC_2MB, &ir->erst_phys);
	if (!ir->erst) {
		printf_serial("[xHCI][ERROR] Failed to allocate the ERST for interrupter %u...\r\n", index);
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[xHCI][ERROR] Failed to allocate the ERST for interrupter %u...\n", index);
		return false;
	}

	for (uint16_t seg = 0; seg < ir->erst_size; seg++) {
		xhci_ring_t* ring = &ir->segments[seg];

		ring->trb_count = XHCI_EVENT_RING_TRBS_PER_SEGMENT;
		ring->trbs = kalloc_dma(ring->trb_count * sizeof(trb_t), DMA_ALIGN_64 | DMA_ZONE_ANY, PDE_FLAGS_UC_2MB, &ring->trbs_phys);
		if (!ring->trbs) {
			printf_serial("[xHCI][ERROR] Failed to allocate event ring segment %u for interrupter %u...\r\n", seg, index);
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[xHCI][ERROR] Failed to allocate event ring segment %u for interrupter %u...\n", seg, index);
			return false;
		}
		ring->enqueue = 0; // unused by software for event rings; the xHC owns "enqueue"

		ir->erst[seg].base = ring->trbs_phys;
		ir->erst[seg].size = ring->trb_count;
	}

	ir->dequeue_segment = 0;
	ir->dequeue = 0;
	ir->cycle = true; // Consumer Cycle State, toggles each time dequeue wraps back to segment 0

	xhci_memory_fence();

	// Program the runtime registers for this interrupter.
	mmio_write32(&hc->runtime->ir[index].erstsz, ir->erst_size);
	xhci_write_register(&hc->runtime->ir[index].erstba, ir->erst_phys, hc->ac64);
	xhci_interrupter_update_erdp(hc, index); // ERDP -> segment 0, TRB 0, EHB cleared


	return true;
}

static usb_speed_t xhci_get_port_speed_from_psi(xhci_controller_t* hc, uint8_t port, uint8_t psi) {
	uint8_t spec_port = port + 1; // ports as far as we are concerned are zero indexed. the spec has them 1 indexed.
	xhci_extended_compat_t* list = hc->first_xce;

	while (list != NULL) {
		if (list->capability != XEC_SUPPORTED_PROTO) {
			list = list->next_node;
			continue;
		}

		xhci_xec_supported_proto_t* sp = (xhci_xec_supported_proto_t*) list->specific_data;

		if (spec_port < sp->comp_port_offset || spec_port > sp->comp_port_offset + sp->comp_port_count) {
			// printf_serial("[xHCI][DEBUG] port not in range\r\n");

			list = list->next_node;
			continue;
		}

		if (sp->psi_count == 0) {
			// use the default values
			// I have *zero* clue what to do with anything above 4.
			// The spec I was working with only had up to SS+ 10gbps
			switch (psi) {
				case 1: return USB_FULL_SPEED;
				case 2: return USB_LOW_SPEED;
				case 3: return USB_HIGH_SPEED;
				case 4: return USB_SPEED_5GBPS;  // "SuperSpeed Gen 1x1"
				case 5: return USB_SPEED_10GBPS; // "SS+ Gen 2x1"
				case 6: return USB_SPEED_5GBPS;  // "SS+ Gen 1x2"
				case 7: return USB_SPEED_10GBPS; // "SS+ Gen 2x2"
				default: return USB_SPEED_UNKNOWN;
			}
		}

		for (uint8_t i = 0; i < sp->psi_count; i++) {
			xhci_xec_supported_proto_psi_t* entry = &sp->psi_array[i];

			if (entry->psiv != psi) continue;

			uint64_t speed_bps = entry->proto_speed_id_mantissa;

			switch (entry->psie) {
				case 0: //bps
					break; // already in bps
				case 1: //kbps
					speed_bps *= 1000;
					break;
				case 2: //mbps
					speed_bps = speed_bps * 1000 * 1000;
					break;
				case 3: //gbps
					speed_bps = speed_bps * 1000 * 1000 * 1000;
					break;
				default: return USB_SPEED_UNKNOWN; // shouldn't be possible
			}

			// LS            1,500,000 bps
			// FS           12,000,000 bps
			// HS          480,000,000 bps
			// USB3      5,000,000,000 bps
			// USB3     10,000,000,000 bps
			// USB3     20,000,000,000 bps
			// USB4     40,000,000,000 bps
			// USB4     80,000,000,000 bps
			// USB4    120,000,000,000 bps (Asymmetric) (not actually sure how this shows up in reality)

			// printf_serial("[xHCI][DEBUG] bps = %llu\r\n", speed_bps);

			if (speed_bps <= 1500000ULL) return USB_LOW_SPEED;
			if (speed_bps <= 12000000ULL) return USB_FULL_SPEED;
			if (speed_bps <= 480000000ULL) return USB_HIGH_SPEED;
			if (speed_bps <= 5000000000ULL) return USB_SPEED_5GBPS;
			if (speed_bps <= 10000000000ULL) return USB_SPEED_10GBPS;
			if (speed_bps <= 20000000000ULL) return USB_SPEED_20GBPS;
			if (speed_bps <= 40000000000ULL) return USB_SPEED_40GBPS;
			if (speed_bps <= 80000000000ULL) return USB_SPEED_80GBPS;
			if (speed_bps <= 120000000000ULL) return USB_SPEED_120GBPS;

			return USB_SPEED_UNKNOWN;
		}

		return USB_SPEED_UNKNOWN;
	}

	return USB_SPEED_UNKNOWN;
}

size_t xhci_get_port_count(usb_hcd_t* hcd) {
	xhci_controller_t* hc = (xhci_controller_t*) hcd->hcd_data;
	return (size_t) hc->max_ports;
}

int xhci_get_port_status(usb_hcd_t* hcd, uint8_t port, usb_port_status_t* status) {
	if (!hcd | !status) return -1;
	// we have a standardized format for port statuses since the HC specs don't all agree on things (thanks USB-IF...)
	bool connected = false, enabled = false;
	usb_speed_t speed = USB_SPEED_UNKNOWN;

	xhci_controller_t* hc = (xhci_controller_t*) hcd->hcd_data;
	if (port > hc->max_ports - 1) return -2;

	uint32_t portsc = mmio_read32((const volatile void*) &hc->ports[port].portsc);

	uint8_t ccs = FIELD_GET(GENMASK(0, 0), portsc);
	if (ccs != 0) connected = true;
	status->connected = connected;
	if (!connected) return 0; // no reason to keep going, no device is connected

	uint8_t ped = FIELD_GET(GENMASK(1, 1), portsc);
	if (ped != 0) enabled = true;

	uint8_t portsc_psi = FIELD_GET(GENMASK(13, 10), portsc);
	speed = xhci_get_port_speed_from_psi(hc, port, portsc_psi);

	// uint8_t oca = FIELD_GET(GENMASK(3, 3), portsc);

	// I could read PLS here but meh, it's not really required and kinda annoying to parse
	// Port Power is also here, same as before, I don't really care about it.
	// I'm actually going to ignore all the other fields from this register.

	printf_serial("[xHCI] PORT %u STATUS INFO: CCS=%u PED=%u PSI=%u (%s)\r\n", port, ccs, ped, portsc_psi, usb_speed_to_string(speed));

	status->enabled = enabled;
	status->speed = speed;

	return 0;
}

int xhci_reset_port(usb_hcd_t* hcd, uint8_t port) {

}
int xhci_enable_port(usb_hcd_t* hcd, uint8_t port) {

}
int xhci_disable_port(usb_hcd_t* hcd, uint8_t port) {

}
int xhci_device_init(usb_hcd_t* hcd, usb_device_t* dev) {

}
int xhci_device_destroy(usb_hcd_t* hcd, usb_device_t* dev) {

}
int xhci_endpoint_open(usb_hcd_t* hcd, usb_endpoint_t* ep) {

}
int xhci_endpoint_close(usb_hcd_t* hcd, usb_endpoint_t* ep) {

}
int xhci_endpoint_reset(usb_hcd_t* hcd, usb_endpoint_t* ep) {

}
int xhci_execute_transfer(usb_hcd_t* hcd, usb_transfer_t* transfer) {

}

static const usb_hcd_ops_t xhci_ops = {
	// need start, stop, restart. need to refactor a little for this
	// needs async transfers (will wait for scheduling)

	.get_port_count = xhci_get_port_count,
	.get_port_status = xhci_get_port_status,
	.reset_port = xhci_reset_port,
	.enable_port = xhci_enable_port,
	.disable_port = xhci_disable_port,

	.device_init = xhci_device_init,
	.device_destroy = xhci_device_destroy,

	.endpoint_open = xhci_endpoint_open,
	.endpoint_close = xhci_endpoint_close,
	.endpoint_reset = xhci_endpoint_reset,

	.execute_transfer = xhci_execute_transfer,
};

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
	hc->op = (volatile xhci_op_regs_t*) ((uintptr_t) hc->mmio_base + mmio_read8_as32(&hc->cap->caplength));
	hc->ports = (volatile xhci_port_regs_t*) ((uintptr_t) hc->op + 0x400); // ports are at offset 0x400 from the operational register base.
	// ports can be accessed using hc->ports[index], where index is between 0 - (hc->max_ports - 1)

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
	hc->max_slots = max_dev_slots;
	hc->max_ports = max_ports;
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
	hc->doorbell = (volatile xhci_doorbell_regs_t*) ((uintptr_t) hc->mmio_base + dboff);

	/* RTSOFF */
	uint32_t rtsoff = mmio_read32(&hc->cap->rtsoff);
	rtsoff = FIELD_GET(GENMASK(31, 5), rtsoff);
	printf_serial("[xHCI] RTSOFF = 0x%x\r\n", rtsoff);
	hc->runtime = (volatile xhci_runtime_regs_t*) ((uintptr_t) hc->mmio_base + rtsoff);

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

	xhci_print_port_protocols(hc);
	xhci_reset_controller(hc);

	// In order that the spec lists them:
	// - Program MaxSlotsEn in CONFIG
	// - Program DCBAAP
	// - Program the CRCR
	// - Init Interrupts (optional, wont do for now)
	// Write USBCMD R/S bit to 1

	xhci_write_max_slots(hc, max_dev_slots);
	// we need the dcbaap now, 64 bit register so we need to use the wrapper
	// page 441 of spec, section 6.1:
	// The Device Context Base Address Array shall contain MaxSlotsEn + 1 entries.
	// The maximum size of the Device Context Base Address Array is 256 64-bit entries, or 2K Bytes.
	// We need this to be aligned at a 64 byte boundary
	// We also need to write the physical address to the controller
	hc->dcbaa_size = hc->max_slots + 1;
	hc->dcbaa = kalloc_dma(hc->dcbaa_size * sizeof(uint64_t), DMA_ALIGN_64 | DMA_ZONE_ANY, PDE_FLAGS_UC_2MB, &hc->dcbaa_phys);
	if (!hc->dcbaa) {
		printf_serial("[xHCI][ERROR] Failed to allocate the DCBAA... \r\n");
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[xHCI][ERROR] Failed to allocate the DCBAA... \r\n");
		return;
	}
	xhci_memory_fence();
	xhci_write_register(&hc->op->dcbaap, hc->dcbaa_phys, hc->ac64);


	/* I don't know why, but I struggled to understand the point of the CRCR when writing this, so this is a small summary.
	 * The Command Ring is only for commands directed at the controller itself.
	 * - Enable Slot
	 * - Disable Slot
	 * - Address Device
	 * - Configure Endpoint
	 * - Evaluate Context
	 * - Reset Endpoint
	 * - Stop Endpoint
	 *
	 * There is no recommended or defined size requirement, only:
	 * - 64-byte alignment
	 * - no TRB may cross a 64 KiB boundary
	 * We use 4096 only because that's kinda the "standard" that most systems have settled on.
	 * 4096 / 16 = 256 total TRBs (255 actually, last is Link TRB)
	 *
	 * From our point of view, the Command Ring is simply a queue of Command TRBs living in DMA-accessible memory.
	 * We maintain an enqueue pointer indicating the next free TRB.
	 * To submit a command, we write a new TRB at the enqueue position, advance the pointer (wrapping at the Link TRB), and ring Doorbell 0.
	 * The xHC fetches and executes the command via DMA, advancing its own internal dequeue pointer.
	 * Completion is reported asynchronously through the Event Ring.
	 * We do not update CRCR or track the controller's dequeue pointer during normal operation.
	 */
	// The R/S bit is set to zero, so in theory the CRCR should be writeable, but we still check to make sure
	uint64_t crcr_value = xhci_read_register(&hc->op->crcr, hc->ac64);
	if (FIELD_GET(GENMASK(3, 3), crcr_value) != 0) {
		// for some reason the command ring is running, we can try to write command abort
		FIELD_WRITE(crcr_value, GENMASK(2, 2), 1);
		xhci_write_register(&hc->op->crcr, crcr_value, hc->ac64);
		// we're going to give it a little bit of time to stop before we check again.
		xhci_delay_us(5);
		crcr_value = xhci_read_register(&hc->op->crcr, hc->ac64);
		if (FIELD_GET(GENMASK(3, 3), crcr_value) != 0) {
			printf_serial("[xHCI][ERROR] Faulty Command Ring, can't stop it.\r\n");
			printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[xHCI][ERROR] Faulty Command Ring, can't stop it.\n");
			return; // TODO: we need a cleanup of some kind
		}
	}

	// We want to start with a fresh value to avoid copying any lingering config bits
	crcr_value = 0;

	hc->command_ring.enqueue = 0;
	hc->command_ring.cycle = true;

	hc->command_ring.trbs = kalloc_dma(4096, DMA_ALIGN_64 | DMA_ZONE_ANY, PDE_FLAGS_UC_2MB, &hc->command_ring.trbs_phys);
	hc->command_ring.trb_count = 4096 / sizeof(trb_t); // should be 256, we use trb_count everywhere instead of hardcoding it in case we want to increase/decrease this array size eventually
	if (!hc->command_ring.trbs) {
		printf_serial("[xHCI][ERROR] Failed to allocate the CRCR... \r\n");
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[xHCI][ERROR] Failed to allocate the CRCR... \r\n");
		return;
	}

	// Last TLB is the link TLB
	trb_t* link = &hc->command_ring.trbs[hc->command_ring.trb_count - 1];
	memset(link, 0, sizeof(*link)); // should be zero'd already but it doesn't hurt to be sure
	link->parameter = hc->command_ring.trbs_phys;
	FIELD_WRITE(link->control, GENMASK(15, 10), 6); // TRB Type = Link (6)
	FIELD_WRITE(link->control, BIT(1), 1); // Toggle Cycle
	FIELD_WRITE(link->control, BIT(0), 1); // Cycle bit

	FIELD_WRITE(crcr_value, GENMASK(63, 6), hc->command_ring.trbs_phys);
	FIELD_WRITE(crcr_value, BIT(0), 1); // RCS = 1

	xhci_memory_fence();
	xhci_write_register(&hc->op->crcr, crcr_value, hc->ac64);

	// For now we only support one interrupter.
	// Each interrupter has it's own registers:
	// IMAN
	// IMOD
	// ERSTSZ
	// ERSTBA
	// ERDP
	// Event Ring
	// ERST
	hc->interrupter_count = XHCI_INTERRUPTER_COUNT;
	hc->interrupters = kcalloc(hc->interrupter_count, sizeof(xhci_interrupter_t));
	if (!hc->interrupters) {
		printf_serial("[xHCI][ERROR] Failed to allocate interrupters...\r\n");
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[xHCI][ERROR] Failed to allocate interrupters...\n");
		return;
	}

	for (uint16_t i = 0; i < hc->interrupter_count; i++) {
		if (!xhci_init_interrupter(hc, i)) {
			return;
		}
	}

	usb_hcd_t* hcd = (usb_hcd_t*) kcalloc(1, sizeof(usb_hcd_t));
	hcd->ops = &xhci_ops;
	hcd->device = dev;
	hcd->type = USB_HCD_XHCI;
	hcd->hcd_data = (void*) hc; // we just store the entire xhci_controller_t here. This is the same pointer that is in dev->driver_data for the HCD.

	usb_hcd_register(hcd);
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