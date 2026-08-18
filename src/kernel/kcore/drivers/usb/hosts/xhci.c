#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <endian_bits.h>
#include <cpu_io.h>
#include <arch.h>

#include <system/timer.h>

#include <memory/virtual_mem.h>
#include <memory/kernel_alloc.h>

#include <device/device_manager.h>

#include <drivers/driver_manager.h>
#include <drivers/pci.h>
#include <drivers/serial.h>
#include <drivers/usb/hosts/xhci.h>
#include <drivers/usb/usb_core.h>

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Low-level register / timing helpers
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

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
static inline uint64_t xhci_read_register(volatile void* reg, bool ac64) {
	if (ac64) return mmio_read64(reg);
	return (uint64_t) mmio_read32(reg);
}

/**
 * @brief Ensures previous memory operations complete before continuing.
 *
 */
static inline void xhci_memory_fence() {
	cpu_memory_barrier();
}

static inline void xhci_delay_us(uint32_t us) {
	// This is actually (kinda) accurate now...
	busy_wait_us(us);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Generic ring management
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static bool xhci_ring_init(xhci_ring_t* ring, size_t trb_count) {
	if (!ring || trb_count < 2) return false; // need at least 1 real slot + the Link TRB

	ring->trb_count = trb_count;
	ring->enqueue = 0;
	ring->cycle = true;

	ring->trbs = (trb_t*) kalloc_dma(trb_count * sizeof(trb_t), DMA_ZONE_ANY, PDE_FLAGS_UC_2MB, &ring->trbs_phys);
	if (!ring->trbs) return false;
	memset(ring->trbs, 0, trb_count * sizeof(trb_t));

	// Reserve the last slot as a permanent Link TRB, pointing back to the start.
	trb_t* link = &ring->trbs[trb_count - 1];
	link->parameter = ring->trbs_phys;
	FIELD_WRITE(link->control, GENMASK(15, 10), XHCI_TRB_TYPE_LINK);
	FIELD_WRITE(link->control, BIT(1), 1); // Toggle Cycle
	FIELD_WRITE(link->control, BIT(0), 1); // Cycle bit

	xhci_memory_fence();

	return true;
}

static bool xhci_ring_enqueue(xhci_ring_t* ring, const trb_t* trb) {
	if (!ring || !trb) return false;

	trb_t* dst = &ring->trbs[ring->enqueue];

	/* Copy the TRB without its cycle bit. */
	*dst = *trb;
	FIELD_WRITE(dst->control, BIT(0), 0);

	xhci_memory_fence();

	FIELD_WRITE(dst->control, BIT(0), ring->cycle);

	xhci_memory_fence();

	ring->enqueue++;

	/* Link TLB */
	if (ring->enqueue == ring->trb_count - 1) {
		trb_t* link = &ring->trbs[ring->trb_count - 1];

		FIELD_WRITE(link->control, BIT(0), ring->cycle);
		xhci_memory_fence();

		ring->enqueue = 0;
		ring->cycle = !ring->cycle;
	}

	return true;
}

static inline void xhci_ring_doorbell(xhci_controller_t* hc, uint8_t doorbell, uint16_t target) {
	mmio_write32(&hc->doorbell->db[doorbell], target); // It's up to the caller to get this info correct. We technically could use streams for bulk transfers, but that's more than I'm willing to deal with right now.
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Interrupter / event ring management
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Computes the physical address of the interrupter's current dequeue TRB.
static inline uintptr_t xhci_interrupter_dequeue_phys(xhci_interrupter_t* ir) {
	xhci_ring_t* seg = &ir->segments[ir->dequeue_segment];
	return seg->trbs_phys + (ir->dequeue * sizeof(trb_t));
}

// Advances the interrupter's dequeue pointer by one TRB, wrapping to next segment as needed
// CCS only toggles when we wrap back around to segment 0
static void xhci_interrupter_advance_dequeue(xhci_interrupter_t* ir) {
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
static void xhci_interrupter_update_erdp(xhci_controller_t* hc, uint16_t index) {
	xhci_interrupter_t* ir = &hc->interrupters[index];

	uint64_t erdp_value = xhci_interrupter_dequeue_phys(ir) & GENMASK(63, 4);
	FIELD_WRITE(erdp_value, GENMASK(2, 0), ir->dequeue_segment); // DESI
	FIELD_WRITE(erdp_value, BIT(3), 1); // EHB - RW1C, always write 1 to clear

	xhci_memory_fence();
	xhci_write_register(&hc->runtime->ir[index].erdp, erdp_value, hc->ac64);
}

static bool xhci_init_interrupter(xhci_controller_t* hc, uint16_t index) {
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
		ring->enqueue = 0; // unused by software for event rings

		ir->erst[seg].base = ring->trbs_phys;
		ir->erst[seg].size = ring->trb_count;
	}

	ir->dequeue_segment = 0;
	ir->dequeue = 0;
	ir->cycle = true; // Consumer Cycle State, toggles each time dequeue wraps back to segment 0

	xhci_memory_fence();

	// Program the runtime registers for this interrupter.
	mmio_write32(&hc->runtime->ir[index].erstsz, ir->erst_size);
	xhci_interrupter_update_erdp(hc, index);    // ERDP -> segment 0, TRB 0, EHB cleared
	xhci_write_register(&hc->runtime->ir[index].erstba, ir->erst_phys, hc->ac64);

	return true;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Command ring
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static bool xhci_send_command_and_wait(xhci_controller_t* hc, const trb_t* cmd_trb, trb_t* completion_out) {
	if (!hc || !cmd_trb) return false;

	// Capture the physical address of the slot we're about to write into, BEFORE xhci_ring_enqueue advances hc->command_ring.enqueue.
	uintptr_t cmd_phys = hc->command_ring.trbs_phys + (hc->command_ring.enqueue * sizeof(trb_t));

	if (!xhci_ring_enqueue(&hc->command_ring, cmd_trb)) {
		printf_serial("[xHCI][ERROR] Failed to enqueue command TRB.\r\n");
		return false;
	}
	xhci_ring_doorbell(hc, 0, 0);

	xhci_interrupter_t* ir = &hc->interrupters[0];

	while (true) {
		trb_t* event_trb = (trb_t*) ((uintptr_t) ir->segments[ir->dequeue_segment].trbs + (ir->dequeue * sizeof(trb_t)));
		bool event_cycle = FIELD_GET(BIT(0), event_trb->control) != 0;

		if (event_cycle != ir->cycle) {
			cpu_relax();
			continue;
		}
		xhci_memory_fence();

		uint8_t trb_type = FIELD_GET(GENMASK(15, 10), event_trb->control);

		if (trb_type != XHCI_TRB_TYPE_CMD_COMPLETION) {
			printf_serial("[xHCI][INFO] Consuming side-event type: %u while waiting for command completion.\r\n", trb_type);
			xhci_interrupter_advance_dequeue(ir);
			xhci_interrupter_update_erdp(hc, 0);
			continue;
		}

		if (event_trb->parameter != cmd_phys) {
			// A completion event for some OTHER command TRB
			printf_serial("[xHCI][WARN] Command completion for unexpected TRB (got %llx, wanted %llx)... discarding.\r\n", event_trb->parameter, cmd_phys);
			printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "[xHCI][WARN] Command completion for unexpected TRB (got %llx, wanted %llx)... discarding.\r\n", event_trb->parameter, cmd_phys);
			xhci_interrupter_advance_dequeue(ir);
			xhci_interrupter_update_erdp(hc, 0);
			continue;
		}

		if (completion_out) *completion_out = *event_trb;
		xhci_interrupter_advance_dequeue(ir);
		xhci_interrupter_update_erdp(hc, 0);
		return true;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Slot management
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

void xhci_enable_slot(xhci_controller_t* hc, uint8_t port, uint8_t* slot_id_out) {
	if (slot_id_out) *slot_id_out = 0;

	// first order of business is to get the slot type from the xce
	uint8_t spec_port = port + 1;  // ports as far as we are concerned are zero indexed. the spec has them 1 indexed.
	xhci_extended_compat_t* list = hc->first_xce;
	uint8_t slot_type = 0;

	while (list != NULL) {
		if (list->capability != XEC_SUPPORTED_PROTO) {
			list = list->next_node;
			continue;
		}

		xhci_xec_supported_proto_t* sp = (xhci_xec_supported_proto_t*) list->specific_data;

		if (spec_port < sp->comp_port_offset || spec_port >= (sp->comp_port_offset + sp->comp_port_count)) {
			list = list->next_node;
			continue;
		}

		slot_type = sp->proto_slot_type;
		break;
	}

	// We have the slot type we need. All we need to do is write the TRB.
	// Enable slot only uses DWORD3 for the TRB, everything else is reserved so we will write all zeros.
	trb_t cmd = { 0 };
	FIELD_WRITE(cmd.control, GENMASK(20, 16), slot_type);
	FIELD_WRITE(cmd.control, GENMASK(15, 10), XHCI_COMMAND_TRB_ENABLE_SLOT);
	// cycle bit is handled for us by xhci_ring_enqueue via xhci_send_command_and_wait

	trb_t completion;
	if (!xhci_send_command_and_wait(hc, &cmd, &completion)) {
		printf_serial("[xHCI][ERROR] Enable Slot command failed to complete.\r\n");
		return;
	}

	uint8_t comp_code = FIELD_GET(GENMASK(31, 24), completion.status);
	if (comp_code != 1) {
		printf_serial("[xHCI][ERROR] Enable Slot failed with comp code: %u\r\n", comp_code);
		return;
	}

	if (slot_id_out) *slot_id_out = FIELD_GET(GENMASK(31, 24), completion.control);
}

bool xhci_disable_slot(xhci_controller_t* hc, uint8_t slot_id) {
	if (!hc || !slot_id) return false;

	trb_t cmd = { 0 };
	FIELD_WRITE(cmd.control, GENMASK(15, 10), XHCI_COMMAND_TRB_DISABLE_SLOT);
	FIELD_WRITE(cmd.control, GENMASK(31, 24), slot_id);

	trb_t completion;
	if (!xhci_send_command_and_wait(hc, &cmd, &completion)) {
		printf_serial("[xHCI][ERROR] Disable Slot command failed to complete (slot=%u).\r\n", slot_id);
		return false;
	}

	uint8_t comp_code = FIELD_GET(GENMASK(31, 24), completion.status);
	if (comp_code != 1) {
		printf_serial("[xHCI][ERROR] Disable Slot failed with comp code: %u (slot=%u)\r\n", comp_code, slot_id);
		return false;
	}

	return true;
}

void xhci_write_max_slots(xhci_controller_t* hc, uint8_t slots) {
	if (!hc || slots == 0) return;

	uint32_t config_reg = mmio_read32(&hc->op->config);
	FIELD_WRITE(config_reg, MASK_32_BYTE0, slots);
	mmio_write32(&hc->op->config, config_reg);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Capability parsing and printing
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static void xhci_parse_hccparams1(uint32_t raw, hccparams1_t* hccp1) {
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

static void xhci_print_hccparams1(const hccparams1_t* hccp1) {
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

static void xhci_parse_hccparams2(uint32_t raw, hccparams2_t* hccp2) {
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

static void xhci_print_hccparams2(const hccparams2_t* hccp2) {
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

static void xhci_parse_supported_protocol(uintptr_t cap_base, xhci_xec_supported_proto_t* proto) {
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

static void xhci_print_supported_protocol(const xhci_xec_supported_proto_t* proto) {
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

static void xhci_print_port_protocols(xhci_controller_t* hc) {
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

static void xhci_parse_legacy_support(uintptr_t cap_base, xhci_xec_legacy_support_t* legsup) {
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

static void xhci_print_legacy_support(const xhci_xec_legacy_support_t* legsup) {
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

static xhci_xec_capability_id_t get_id_from_value(uint8_t value) {
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

static void xhci_bios_handoff(uintptr_t cap_base, xhci_xec_legacy_support_t* legsup) {
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

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Controller reset
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

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

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Port speed / transfer status helpers
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

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
			// I have *zero* clue what to do with anything above 7.
			// The spec I was working with only had up to SS+ 10gbps
			// I have zero clue how USB4 devices will show up here, and no devices to test it with.
			// I assume that USB4 devices will just use the actual PSI arrays.
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

			printf_serial("[xHCI][DEBUG] bps = %llu\r\n", speed_bps);

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

static usb_transfer_status_t xhci_completion_code_to_status(uint8_t comp_code) {
	switch (comp_code) {
		case 1:  return USB_TRANSFER_COMPLETED;      // Success
		case 13: return USB_TRANSFER_COMPLETED;      // Short Packet (not an error)
		case 6:  return USB_TRANSFER_ERROR_STALL;    // Stall Error
		case 3:  return USB_TRANSFER_ERROR_BABBLE;   // Babble Detected Error
		case 4:  return USB_TRANSFER_ERROR_CRC;      // USB Transaction Error (bus-level NAK/CRC/timeout, HC retried and gave up)
		default: return USB_TRANSFER_ERROR_HARDWARE;
	}
}

static bool xhci_wait_transfer_event(xhci_controller_t* hc, uintptr_t setup_trb_phys, uintptr_t data_trb_phys, uintptr_t status_trb_phys, trb_t* completion_out, size_t timeout_ms) {
	xhci_interrupter_t* ir = &hc->interrupters[0];
	// Polled in ~10us steps
	// Time out 0 is "wait forever", shouldn't really be used
	size_t remaining_us = timeout_ms * 1000;
	const size_t poll_step_us = 10;

	while (true) {
		trb_t* event_trb = (trb_t*) ((uintptr_t) ir->segments[ir->dequeue_segment].trbs + (ir->dequeue * sizeof(trb_t)));
		bool event_cycle = FIELD_GET(BIT(0), event_trb->control) != 0;

		if (event_cycle != ir->cycle) {
			cpu_relax();

			if (timeout_ms != 0) {
				xhci_delay_us(poll_step_us);
				if (remaining_us <= poll_step_us) {
					printf_serial("[xHCI][WARN] Timed out waiting for transfer event.\r\n");
					return false;
				}
				remaining_us -= poll_step_us;
			}
			continue;
		}
		xhci_memory_fence();

		uint8_t trb_type = FIELD_GET(GENMASK(15, 10), event_trb->control);

		if (trb_type != XHCI_TRB_TYPE_TRANSFER_EVENT) {
			printf_serial("[xHCI][INFO] Consuming side-event type: %u while waiting for transfer completion.\r\n", trb_type);
			xhci_interrupter_advance_dequeue(ir);
			xhci_interrupter_update_erdp(hc, 0);
			continue;
		}

		uintptr_t p = event_trb->parameter;
		bool is_setup = (p == setup_trb_phys);
		bool is_data = (data_trb_phys != 0 && p == data_trb_phys);
		bool is_status = (p == status_trb_phys);

		if (!is_setup && !is_data && !is_status) {
			printf_serial("[xHCI][WARN] Transfer event for unrelated TRB (got %llx)\r\n", (unsigned long long) p);
			xhci_interrupter_advance_dequeue(ir);
			xhci_interrupter_update_erdp(hc, 0);
			continue;
		}

		uint8_t comp_code = FIELD_GET(GENMASK(31, 24), event_trb->status);

		if (completion_out) *completion_out = *event_trb;
		xhci_interrupter_advance_dequeue(ir);
		xhci_interrupter_update_erdp(hc, 0);

		// Status stage completes the transfer
		// Any other completion code is terminal.
		// The xHC will not continue the ring after an error (e.g. STALL).
		if (is_status || comp_code != 1) {
			return true;
		}

		// Non error setup/data event
		// keep waiting for the Status TRB completion.
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Port operations (for usb_hcd interface)
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

size_t xhci_get_port_count(usb_hcd_t* hcd) {
	xhci_controller_t* hc = (xhci_controller_t*) hcd->hcd_data;
	return (size_t) hc->max_ports;
}

int xhci_get_port_status(usb_hcd_t* hcd, uint8_t port, usb_port_status_t* status) {
	if (!hcd || !status) return -1;
	// we have a standardized format for port statuses since the HC specs don't all agree on things (thanks USB-IF...)
	bool connected = false, enabled = false;
	usb_speed_t speed = USB_SPEED_UNKNOWN;

	xhci_controller_t* hc = (xhci_controller_t*) hcd->hcd_data;
	if (port >= hc->max_ports) return -2;
	// int i = 0;
	uint32_t portsc = mmio_read32((volatile void*) &hc->ports[port].portsc);
	// for (; i < 100; i++) {
	// 	portsc = mmio_read32((volatile void*) &hc->ports[port].portsc);

	// 	if (FIELD_GET(GENMASK(0, 0), portsc)) break;

	// 	xhci_delay_us(1000);
	// }
	// if (FIELD_GET(GENMASK(0, 0), portsc)) {
	// 	if (i > 0)
	// 		printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "[XHCI] took %d to clear\r\n", i);
	// }

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

#define PORTSC_RW_MASK \
    (GENMASK(8, 5)  |  /* PLS */ \
     BIT(9)         |  /* PP */  \
     GENMASK(15, 14) | /* PIC */ \
     BIT(16)        |  /* LWS */ \
     GENMASK(27, 25))  /* WCE/WDE/WOE */

int xhci_reset_port(usb_hcd_t* hcd, uint8_t port) {
	if (!hcd) return -1;
	// we have a standardized format for port statuses since the HC specs don't all agree on things (thanks USB-IF...)
	xhci_controller_t* hc = (xhci_controller_t*) hcd->hcd_data;
	if (port > hc->max_ports - 1) return -2;

	// All we need to do is set the portsc PR to 1
	// The host controller will clear the PED bit to 0
	// USB3 allows for "warm resets", which we're just going to ignore.
	// Reset port is called mostly only during enumeration, and hot resets apply the same path for USB2 and USB3

	uint32_t portsc = mmio_read32((volatile void*) &hc->ports[port].portsc);

	FIELD_WRITE(portsc, GENMASK(4, 4), 1); // slot 4 is the Port Reset flag
	mmio_write32((volatile void*) &hc->ports[port].portsc, portsc & (PORTSC_RW_MASK | GENMASK(4, 4)));

	xhci_delay_us(10); // very generous delay to let the controller handle this

	int timeout = 100; // 10ms
	while (FIELD_GET(GENMASK(4, 4), mmio_read32((volatile void*) &hc->ports[port].portsc)) != 0) {
		xhci_delay_us(timeout * 1000);
		timeout--;
		if (timeout <= 0) break;
	}

	if (timeout <= 0) {
		printf_serial("[xHCI][WARN] Port %u reset timed out\r\n", port);
		printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "[xHCI][WARN] Port %u reset timed out\r\n", port);
		return -1;
	}

	portsc = mmio_read32((volatile void*) &hc->ports[port].portsc);
	// Copied from spec 4.19.5:
	// If the bus reset sequence completes successfully, the xHC shall update the PORTSC register:
	// - Set the PLS field to U0 ('0').
	// - Clear the PR bit ('0').
	// - Set PED to the enabled state ('1').
	// - Set the PRC bit ('1').
	// - For a USB3 protocol port, if a Hot Reset transitioned to a Warm Reset, set the WRC bit ('1'). <- we dont care about this one, it's not worth the extra effort to check
	// - Set Port Speed field to the speed of the newly attached device.
	//
	// If the bus reset sequence does NOT complete successfully, the xHC shall update the PORTSC register:
	// - Set the PLS field to RxDetect ('5').
	// - Clear the PR bit ('0').
	// - Set the PRC bit ('1').
	// - For a USB3 protocol port, if a Hot Reset transitioned to a Warm Reset, set the WRC bit ('1').
	// - Set the Port Speed field to Undefined Speed ('0').
	// - Clear the CCS bit ('0').

	uint8_t ccs = FIELD_GET(GENMASK(0, 0), portsc);
	uint8_t ped = FIELD_GET(GENMASK(1, 1), portsc);
	uint8_t pr = FIELD_GET(GENMASK(4, 4), portsc); // in theory should be zero. if it's 1, we're in a lot of trouble and will probably leave the port alone. 
	uint8_t pls = FIELD_GET(GENMASK(8, 5), portsc);
	uint8_t prc = FIELD_GET(GENMASK(21, 21), portsc);

	FIELD_WRITE(portsc, GENMASK(21, 21), 1);
	// We're going to write back 1 to PRC to clear it, regardless of what happens
	mmio_write32((volatile void*) &hc->ports[port].portsc, portsc & (PORTSC_RW_MASK | GENMASK(21, 21)));

	if (ccs == 0) {
		printf_serial("[xHCI][WARN] Failed to reset port %u. (CCS=0)\r\n", port);
		printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "[xHCI][WARN] Failed to reset port %u. (CCS=0)\r\n", port);
		return -3;
	}

	if (pr != 0) {
		printf_serial("[xHCI][WARN] Failed to reset port %u. (PR)\r\n", port);
		printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "[xHCI][WARN] Failed to reset port %u. (PR)\r\n", port);
		return -4;
	}
	if (pls != 0) {
		printf_serial("[xHCI][WARN] Failed to reset port %u. (PLS)\r\n", port);
		printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "[xHCI][WARN] Failed to reset port %u. (PLS)\r\n", port);
		return -5;
	}
	if (prc != 1) {
		printf_serial("[xHCI][WARN] Failed to reset port %u. (PRC)\r\n", port);
		printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "[xHCI][WARN] Failed to reset port %u. (PRC)\r\n", port);
		return -6;
	}
// we kinda ignore the "not sucessfully completed" conditions, but if they don't satisfy the completion sequence then is there really a point in checking? 

// we will "attempt" to enable this again later. USB core doesn't explicitly require us to re-enable the port on port reset
	if (ped != 1) {
		printf_serial("[xHCI][WARN] Port %u is not enabled after reset...\r\n", port);
		printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "[xHCI][WARN] Port is not enabled after reset...\r\n");
	}
	printf_serial("[xHCI] Port %u successfully reset.\r\n", port);
	return 0;
}

int xhci_enable_port(usb_hcd_t* hcd, uint8_t port) {
	if (!hcd) return -1;

	xhci_controller_t* hc = (xhci_controller_t*) hcd->hcd_data;
	if (port > hc->max_ports - 1) return -2;

	// on xhci, the HC should in theory automatically try to get the port to an enabled state on reboot.
	// we will check here if it's not enabled. if it's not enabled (and there's something connected), we will attempt to figure out why.
	// We will attempt another port reset as a best effort attempt at fixing it. 
	volatile void* portsc_reg = (volatile void*) &hc->ports[port].portsc;

	uint32_t portsc = mmio_read32(portsc_reg);

	uint8_t ccs = FIELD_GET(GENMASK(0, 0), portsc);
	uint8_t ped = FIELD_GET(GENMASK(1, 1), portsc);
	uint8_t pr = FIELD_GET(GENMASK(4, 4), portsc);
	uint8_t pls = FIELD_GET(GENMASK(8, 5), portsc);

	if (ped) {
		printf_serial("[xHCI] Port %u is enabled\r\n", port);
		return 0;
	}

	printf_serial("[xHCI][WARN] Port %u is not enabled (CCS=%u PR=%u PLS=%u)\r\n", port, ccs, pr, pls);

	// No device connected. There isn't anything useful to enable.
	if (!ccs) {
		printf_serial("[xHCI][WARN] Port %u has no connected device, it shouldn't've got here...\r\n", port);
		return -3;
	}

	// A reset is still in progress. Don't interfere with it.
	if (pr) {
		printf_serial("[xHCI][WARN] Port %u is still being reset...\r\n", port);
		return -4;
	}

	// Connected but not enabled. 
	// Likely has something weird in the PLS, probably meaning something went wrong during link
	if (pls != 0) {
		printf_serial("[xHCI][WARN] Port %u is connected but not in U0 (PLS=%u)\r\n", port, pls);
		// We can technically recover from this.
		// These are the potential recoverable paths, I'm too lazy to implement this without reason, so I wrote this for future reference:
		//     RxDetect  - link is looking for a receiver. This may indicate failed link initialization.
		//     Polling   - USB3 link training is in progress. Give it some time before deciding that initialization failed.
		//     Recovery  - link recovery/retraining is in progress. Wait.
		//     Resume    - link is transitioning out of a suspended state. Wait.
		//     U1/U2/U3  - valid low-power/suspend states, not necessarily errors. We can set U0 from here, but technically not correct on a hot reset.
		//     Hot Reset - a USB3 reset is already in progress (somehow).
		//     Inactive  - link isn't operational. A fresh port reset may be a reasonable recovery attempt.
	}

	// Really the only thing we can do is attempt another port reset.
	printf_serial("[xHCI] Attempting to reset port %u again to enable it...\r\n", port);

	int ret = xhci_reset_port(hcd, port);
	if (ret != 0) return ret;

	portsc = mmio_read32(portsc_reg);

	ped = FIELD_GET(GENMASK(1, 1), portsc);
	ccs = FIELD_GET(GENMASK(0, 0), portsc);

	if (!ccs) {
		printf_serial("[xHCI][WARN] Port %u lost connection during recovery\r\n", port);
		return -5;
	}

	if (!ped) {
		printf_serial("[xHCI][WARN] Port %u remains disabled after recovery\r\n", port);
		return -6;
	}

	return 0;
}

int xhci_disable_port(usb_hcd_t* hcd, uint8_t port) {
	(void) hcd;
	(void) port;
	return 0; // I don't really care about this ngl. Will come back when it's actually needed.
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Device lifecycle
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

int xhci_device_init(usb_hcd_t* hcd, usb_device_t* dev) {
	if (!hcd || !dev) return -1;

	xhci_controller_t* hc = (xhci_controller_t*) hcd->hcd_data;
	if (dev->port > hc->max_ports - 1) return -2;

	// First step of device init is to get a device slot id.
	uint8_t slot_id = 0;
	xhci_enable_slot(hc, dev->port, &slot_id);
	if (!slot_id) {
		printf_serial("[xHCI][ERROR] Enable Slot failed to return a slot ID.\r\n");
		return -1;
	}
	printf_serial("[xHCI] Got slot %u for port %u\r\n", slot_id, dev->port);

	uint32_t ctx_size = hc->csz ? 64 : 32;

	xhci_device_t* xdev = (xhci_device_t*) kcalloc(1, sizeof(xhci_device_t));
	if (!xdev) {
		printf_serial("[xHCI][ERROR] Failed to allocate xhci_device_t.\r\n");
		return -3;
	}
	xdev->slot_id = slot_id;
	xdev->ctx_size = ctx_size;

	/* Input Context */
	// ONE contiguous allocation, 33 entries
	// Totally didn't think these were separate allocations and waste several hours...
	xdev->input_ctx_base = (uint8_t*) kalloc_dma(33 * ctx_size, DMA_ZONE_ANY, PDE_FLAGS_UC_2MB, &xdev->input_ctx_phys);
	if (!xdev->input_ctx_base) {
		printf_serial("[xHCI][ERROR] Failed to allocate input context.\r\n");
		kfree(xdev);
		return -3;
	}
	memset(xdev->input_ctx_base, 0, 33 * ctx_size);

	xhci_input_context_t* ic = (xhci_input_context_t*) (xdev->input_ctx_base + 0 * ctx_size);
	xhci_slot_context_t* sc = (xhci_slot_context_t*) (xdev->input_ctx_base + 1 * ctx_size);
	xhci_ep_context_t* ep0 = (xhci_ep_context_t*) (xdev->input_ctx_base + 2 * ctx_size);

	FIELD_WRITE(ic->add_context0, GENMASK(1, 0), 0b11); // write A0 and A1, slot context and EP0

	/* Slot Context */

	uint32_t portsc = mmio_read32((volatile void*) &hc->ports[dev->port].portsc);
	uint8_t psiv = FIELD_GET(GENMASK(13, 10), portsc);


	// We don't support hubs right now, so we're going to ignore the route string.
	// TODO: If we get hubs, we need the route string.
	// bit 25 is "MTT", hub field, we ignore
	// bit 26 literally says "this is a hub"
	// During init, context entries should be 1 (endpoint 0)
	// We will change this later during discovery if we need other endpoints
	FIELD_WRITE(sc->dword0, GENMASK(31, 27), 1);       // Context Entries = 1
	// Spec version 1.2 states that port speed is deprecated, easy to set regardless
	FIELD_WRITE(sc->dword0, GENMASK(23, 20), psiv);    // Speed 

	FIELD_WRITE(sc->dword1, GENMASK(23, 16), dev->port + 1); // Root Hub Port Number
	// 32:24 are for hub
	// dword2 is basically all hub stuff

	// EP0's max packet size is a genuine guess until we've read the real device descriptor. 
	// 8 is always legal regardless of actual speed/value, so we're using it as the default value
	// HS & USB3 (and 4) devices have different default values depending on speed (albeit, 8 is still legal)
	usb_speed_t speed = xhci_get_port_speed_from_psi(hc, dev->port, psiv);
	uint16_t max_packet;
	switch (speed) {
		case USB_HIGH_SPEED: max_packet = 64; break;
		case USB_SPEED_5GBPS:
		case USB_SPEED_10GBPS:
		case USB_SPEED_20GBPS:
		case USB_SPEED_40GBPS:
		case USB_SPEED_80GBPS:
		case USB_SPEED_120GBPS: max_packet = 512; break;
		default: max_packet = 8; break;
	}
	xdev->ep0_max_packet_size = max_packet;

	if (!xhci_ring_init(&xdev->ep0_ring, XHCI_TRANSFER_RING_TRB_COUNT)) {
		printf_serial("[xHCI][ERROR] Failed to allocate EP0 transfer ring.\r\n");
		kfree_dma(xdev->input_ctx_base);
		kfree(xdev);
		return -3;
	}

	FIELD_WRITE(ep0->dword1, GENMASK(5, 3), 4); // EP Type = Control
	FIELD_WRITE(ep0->dword1, GENMASK(2, 1), 3); // CErr = 3
	FIELD_WRITE(ep0->dword1, GENMASK(31, 16), max_packet);
	ep0->tr_dequeue_ptr = (xdev->ep0_ring.trbs_phys & ~0xFULL) | 1; // DCS = 1, matches ring->cycle
	FIELD_WRITE(ep0->avg_trb_length, GENMASK(15, 0), 8);

	/* Output Device Context */
	// separate allocation, 32 entries, lives in DCBAA
	xdev->dev_ctx_base = (uint8_t*) kalloc_dma(32 * ctx_size, DMA_ZONE_ANY, PDE_FLAGS_UC_2MB, &xdev->dev_ctx_phys);
	if (!xdev->dev_ctx_base) {
		printf_serial("[xHCI][ERROR] Failed to allocate output device context.\r\n");
		kfree_dma(xdev->ep0_ring.trbs);
		kfree_dma(xdev->input_ctx_base);
		kfree(xdev);
		return -3;
	}
	memset(xdev->dev_ctx_base, 0, 32 * ctx_size);
	hc->dcbaa[slot_id] = (uint64_t) xdev->dev_ctx_phys;

	/* Address Device command */
	trb_t cmd = { 0 };
	cmd.parameter = xdev->input_ctx_phys;
	FIELD_WRITE(cmd.control, GENMASK(15, 10), XHCI_COMMAND_TRB_ADDRESS_DEVICE);
	FIELD_WRITE(cmd.control, GENMASK(31, 24), slot_id);
	// BSR (bit 9) stays 0, full address, not block set address

	trb_t completion;
	if (!xhci_send_command_and_wait(hc, &cmd, &completion)) {
		printf_serial("[xHCI][ERROR] Address Device command failed to complete.\r\n");
		goto fail_cleanup;
	}

	uint8_t comp_code = FIELD_GET(GENMASK(31, 24), completion.status);
	if (comp_code != 1) {
		printf_serial("[xHCI][ERROR] Address Device completed with error code %u.\r\n", comp_code);
		goto fail_cleanup;
	}

	// Slot Context (in the OUTPUT device context, not the input) dword3 bits 7:0 hold the USB device address the xHC actually assigned
	uint32_t* out_slot_ctx = (uint32_t*) (xdev->dev_ctx_base + 1 * ctx_size);
	uint8_t usb_address = FIELD_GET(GENMASK(7, 0), out_slot_ctx[3]);
	printf_serial("[xHCI] Device addressed successfully: slot=%u usb_addr=%u\r\n", slot_id, usb_address);

	xhci_endpoint_t* xep0 = (xhci_endpoint_t*) kcalloc(1, sizeof(xhci_endpoint_t));
	if (!xep0) {
		printf_serial("[xHCI][ERROR] Failed to allocate EP0 hcd_data.\r\n");
		goto fail_cleanup;
	}
	xep0->ring = xdev->ep0_ring;
	xep0->dci = 1;
	xep0->max_packet_size = xdev->ep0_max_packet_size;

	dev->endpoints = (usb_endpoint_t*) kcalloc(1, sizeof(usb_endpoint_t));
	if (!dev->endpoints) {
		printf_serial("[xHCI][ERROR] Failed to allocate endpoints array.\r\n");
		kfree(xep0);
		goto fail_cleanup;
	}
	dev->endpoints[0] = (usb_endpoint_t){
		.address = 0,
		.number = 0,
		.direction = USB_DIR_OUT,
		.type = USB_ENDPOINT_TYPE_CONTROL,
		.max_packet_size = xdev->ep0_max_packet_size,
		.interval = 0,
		.device = dev,
		.hcd_data = xep0,
	};
	dev->endpoint_count = 1;

	dev->address = usb_address;
	dev->hcd_data = xdev;

	return 0;

fail_cleanup:
	hc->dcbaa[slot_id] = 0;
	xhci_disable_slot(hc, slot_id);

	kfree_dma(xdev->dev_ctx_base);
	kfree_dma(xdev->ep0_ring.trbs);
	kfree_dma(xdev->input_ctx_base);
	kfree(xdev);
	return -4;
}

int xhci_device_destroy(usb_hcd_t* hcd, usb_device_t* dev) {
	(void) hcd;
	(void) dev;
	// TODO: this
	return 0;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Endpoint operations
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

int xhci_endpoint_open(usb_hcd_t* hcd, usb_endpoint_t* ep) {
	(void) hcd;
	(void) ep;
	// TODO: This
	return 0;
}

int xhci_endpoint_close(usb_hcd_t* hcd, usb_endpoint_t* ep) {
	(void) hcd;
	(void) ep;
	// TODO: This
	return 0;
}

int xhci_endpoint_reset(usb_hcd_t* hcd, usb_endpoint_t* ep) {
	(void) hcd;
	(void) ep;
	// TODO: This
	return 0;
}

static bool xhci_recover_halted_endpoint(xhci_controller_t* hc, xhci_device_t* xdev, xhci_endpoint_t* xep) {
	if (!hc || !xdev || !xep) return false;

	/* Reset Endpoint */
	trb_t reset_cmd = { 0 };
	FIELD_WRITE(reset_cmd.control, GENMASK(15, 10), XHCI_TRB_TYPE_RESET_ENDPOINT);
	FIELD_WRITE(reset_cmd.control, GENMASK(20, 16), xep->dci);
	FIELD_WRITE(reset_cmd.control, GENMASK(31, 24), xdev->slot_id);
	// TSP (bit 9) left 0 we don't need to preserve transfer state for control endpoints

	trb_t completion;
	if (!xhci_send_command_and_wait(hc, &reset_cmd, &completion)) {
		printf_serial("[xHCI][ERROR] Reset Endpoint command failed to complete (slot=%u dci=%u).\r\n", xdev->slot_id, xep->dci);
		return false;
	}

	uint8_t comp_code = FIELD_GET(GENMASK(31, 24), completion.status);
	if (comp_code != 1) {
		printf_serial("[xHCI][ERROR] Reset Endpoint completed with error code %u (slot=%u dci=%u).\r\n", comp_code, xdev->slot_id, xep->dci);
		return false;
	}

	// Set TR Dequeue Pointer 
	// Sync the HC dequeue pointer with our current enqueue position and cycle state, skipping the failed transfer sequence
	uintptr_t new_dequeue_phys = xep->ring.trbs_phys + (xep->ring.enqueue * sizeof(trb_t));

	trb_t set_tr_cmd = { 0 };
	set_tr_cmd.parameter = (new_dequeue_phys & ~0xFULL) | (xep->ring.cycle ? 1 : 0); // bit0 = DCS
	FIELD_WRITE(set_tr_cmd.control, GENMASK(15, 10), XHCI_TRB_TYPE_SET_TR_DEQUEUE_POINTER);
	FIELD_WRITE(set_tr_cmd.control, GENMASK(20, 16), xep->dci);
	FIELD_WRITE(set_tr_cmd.control, GENMASK(31, 24), xdev->slot_id);

	if (!xhci_send_command_and_wait(hc, &set_tr_cmd, &completion)) {
		printf_serial("[xHCI][ERROR] Set TR Dequeue Pointer command failed to complete (slot=%u dci=%u).\r\n", xdev->slot_id, xep->dci);
		return false;
	}

	comp_code = FIELD_GET(GENMASK(31, 24), completion.status);
	if (comp_code != 1) {
		printf_serial("[xHCI][ERROR] Set TR Dequeue Pointer completed with error code %u (slot=%u dci=%u).\r\n", comp_code, xdev->slot_id, xep->dci);
		return false;
	}

	printf_serial("[xHCI] Endpoint recovered (slot=%u dci=%u).\r\n", xdev->slot_id, xep->dci);
	return true;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Transfer execution
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static void xhci_dump_transfer_timeout_diagnostics(xhci_controller_t* hc, xhci_device_t* xdev, xhci_endpoint_t* xep, usb_device_t* dev) {
	printf("[xHCI][DIAG] transfer timeout diagnostics (slot=%u dci=%u port=%u)\r\n", xdev->slot_id, xep->dci, dev ? dev->port : 0xFF);

	/* Controller-level fault check */
	uint32_t usbsts = mmio_read32(&hc->op->usbsts);
	printf("[xHCI][DIAG] USBSTS=0x%08x  HCH=%u HSE=%u EINT=%u PCD=%u HCE=%u\r\n",
		usbsts,
		FIELD_GET(BIT(0), usbsts),   // HCHalted
		FIELD_GET(BIT(2), usbsts),   // Host System Error
		FIELD_GET(BIT(3), usbsts),   // Event Interrupt pending
		FIELD_GET(BIT(4), usbsts),   // Port Change Detect
		FIELD_GET(BIT(12), usbsts)   // Host Controller Error
	);

	/* Is the device even still there? */
	if (dev) {
		uint32_t portsc = mmio_read32((volatile void*) &hc->ports[dev->port].portsc);
		uint8_t ccs = FIELD_GET(GENMASK(0, 0), portsc);
		uint8_t ped = FIELD_GET(GENMASK(1, 1), portsc);
		uint8_t pls = FIELD_GET(GENMASK(8, 5), portsc);
		printf("[xHCI][DIAG] PORTSC=0x%08x CCS=%u PED=%u PLS=%u\r\n", portsc, ccs, ped, pls);
	}

	/* What does the HC think this endpoint's state is?  */
	uint8_t* ep_ctx_raw = xdev->dev_ctx_base + (xep->dci * xdev->ctx_size);
	uint32_t ep_dword0 = *(uint32_t*) ep_ctx_raw;
	uint8_t ep_state = FIELD_GET(GENMASK(2, 0), ep_dword0);
	static const char* ep_state_names[] = { "Disabled", "Running", "Halted", "Stopped", "Error", "?", "?", "?" };
	printf("[xHCI][DIAG] EP Context state = %u (%s)\r\n", ep_state, ep_state_names[ep_state & 0x7]);

	/* Event ring bookkeeping */
	xhci_interrupter_t* ir = &hc->interrupters[0];
	uint64_t erdp = xhci_read_register(&hc->runtime->ir[0].erdp, hc->ac64);
	uintptr_t our_dequeue_phys = ir->segments[ir->dequeue_segment].trbs_phys + (ir->dequeue * sizeof(trb_t));
	printf("[xHCI][DIAG] SW dequeue phys=0x%llx  HW ERDP=0x%llx  SW cycle=%u\r\n", (unsigned long long) our_dequeue_phys, (unsigned long long) (erdp & ~0xFULL), ir->cycle);

	/* What's actually sitting at our dequeue pointer right now, regardless of whether the cycle bit matched?  */
	trb_t* raw = (trb_t*) ((uintptr_t) ir->segments[ir->dequeue_segment].trbs + (ir->dequeue * sizeof(trb_t)));
	printf("[xHCI][DIAG] raw event slot: param=0x%llx status=0x%08x control=0x%08x (cycle bit=%u)\r\n", (unsigned long long) raw->parameter, raw->status, raw->control, FIELD_GET(BIT(0), raw->control));
}

// xhci_execute_transfer() return codes. Negative values are failures, 0 is success.
#define XHCI_TX_ERR_INVALID_PARAMS      -1  /* Null hcd, transfer, endpoint, or device */
#define XHCI_TX_ERR_UNSUPPORTED_EP_TYPE -2  /* Non-control endpoint */
#define XHCI_TX_ERR_MISSING_SETUP       -3  /* Control transfer missing setup packet */
#define XHCI_TX_ERR_MISSING_HCD_DATA    -4  /* Controller/device/endpoint hcd_data is null */
#define XHCI_TX_ERR_BOUNCE_ALLOC_FAILED -5  /* DMA bounce buffer allocation failed */
#define XHCI_TX_ERR_TIMEOUT             -6  /* Transfer timed out or hardware wait failed */
#define XHCI_TX_ERR_RECOVERY_FAILED     -7  /* Transfer and endpoint recovery both failed */

// Encodes xHCI completion codes (spec table 6.30) (not including success) into: -(100 + comp_code)
// For example: STALL ERROR (comp_code=6) returns -106
#define XHCI_TX_ERR_COMPLETION_BASE      (-100)
#define XHCI_TX_COMPLETION_CODE_FROM_RC(rc)  (-(rc) - 100)

int xhci_execute_transfer(usb_hcd_t* hcd, usb_transfer_t* transfer) {
	if (!hcd || !transfer || !transfer->endpoint || !transfer->device) return XHCI_TX_ERR_INVALID_PARAMS;

	if (transfer->endpoint->type != USB_ENDPOINT_TYPE_CONTROL) {
		printf_serial("[xHCI][ERROR] execute_transfer: only control endpoints supported right now.\r\n");
		return XHCI_TX_ERR_UNSUPPORTED_EP_TYPE;
	}
	if (!transfer->setup) {
		printf_serial("[xHCI][ERROR] execute_transfer: control transfer missing setup packet.\r\n");
		return XHCI_TX_ERR_MISSING_SETUP;
	}

	xhci_controller_t* hc = (xhci_controller_t*) hcd->hcd_data;
	xhci_device_t* xdev = (xhci_device_t*) transfer->device->hcd_data;
	xhci_endpoint_t* xep = (xhci_endpoint_t*) transfer->endpoint->hcd_data;
	if (!hc || !xdev || !xep) return XHCI_TX_ERR_MISSING_HCD_DATA;

	bool has_data = transfer->length > 0;
	bool data_dir_in = (transfer->setup->bmRequestType & 0x80) != 0;

	/* Bounce buffer for the data stage, if any */
	void* bounce = NULL;
	uintptr_t bounce_phys = 0;
	if (has_data) {
		bounce = kalloc_dma(transfer->length, DMA_ZONE_ANY, PDE_FLAGS_UC_2MB, &bounce_phys);
		if (!bounce) {
			printf_serial("[xHCI][ERROR] execute_transfer: failed to allocate bounce buffer.\r\n");
			return XHCI_TX_ERR_BOUNCE_ALLOC_FAILED;
		}
		if (!data_dir_in) {
			memcpy(bounce, transfer->buffer, transfer->length);
		}
	}

	/* Setup Stage TRB */
	size_t setup_index = xep->ring.enqueue;
	uintptr_t setup_trb_phys = xep->ring.trbs_phys + (setup_index * sizeof(trb_t));

	trb_t setup_trb = { 0 };
	memcpy(&setup_trb.parameter, transfer->setup, sizeof(usb_setup_packet_t));
	FIELD_WRITE(setup_trb.status, GENMASK(16, 0), sizeof(usb_setup_packet_t));
	FIELD_WRITE(setup_trb.control, GENMASK(15, 10), XHCI_TRB_TYPE_SETUP_STAGE);
	FIELD_WRITE(setup_trb.control, BIT(6), 1);
	if (has_data) {
		FIELD_WRITE(setup_trb.control, GENMASK(17, 16), data_dir_in ? 3 : 2);
	}
	xhci_ring_enqueue(&xep->ring, &setup_trb);

	/* Data Stage TRB (optional) */
	uintptr_t data_trb_phys = 0;
	if (has_data) {
		size_t data_index = xep->ring.enqueue;
		data_trb_phys = xep->ring.trbs_phys + (data_index * sizeof(trb_t));

		trb_t data_trb = { 0 };
		data_trb.parameter = bounce_phys;
		FIELD_WRITE(data_trb.status, GENMASK(16, 0), (uint32_t) transfer->length);
		FIELD_WRITE(data_trb.control, GENMASK(15, 10), XHCI_TRB_TYPE_DATA_STAGE);
		FIELD_WRITE(data_trb.control, BIT(16), data_dir_in ? 1 : 0);
		xhci_ring_enqueue(&xep->ring, &data_trb);
	}

	/* Status Stage TRB */
	size_t status_index = xep->ring.enqueue;
	uintptr_t status_trb_phys = xep->ring.trbs_phys + (status_index * sizeof(trb_t));

	trb_t status_trb = { 0 };
	bool status_dir_in = has_data ? !data_dir_in : true;
	FIELD_WRITE(status_trb.control, GENMASK(15, 10), XHCI_TRB_TYPE_STATUS_STAGE);
	FIELD_WRITE(status_trb.control, BIT(16), status_dir_in ? 1 : 0);
	FIELD_WRITE(status_trb.control, BIT(5), 1);
	xhci_ring_enqueue(&xep->ring, &status_trb);

	xhci_ring_doorbell(hc, xdev->slot_id, xep->dci);

	trb_t completion;
	if (!xhci_wait_transfer_event(hc, setup_trb_phys, data_trb_phys, status_trb_phys, &completion, transfer->timeout_ms)) {
		transfer->status = USB_TRANSFER_ERROR_HARDWARE;
		xhci_dump_transfer_timeout_diagnostics(hc, xdev, xep, transfer->device);
		if (bounce) kfree_dma(bounce);
		printf_serial("[xHCI][ERROR] execute_transfer: timed out waiting for transfer event.\r\n");
		return XHCI_TX_ERR_TIMEOUT;
	}

	uint8_t comp_code = FIELD_GET(GENMASK(31, 24), completion.status);
	uint32_t residual = FIELD_GET(GENMASK(23, 0), completion.status);

	transfer->status = xhci_completion_code_to_status(comp_code);

	if (transfer->status == USB_TRANSFER_COMPLETED) {
		transfer->actual_length = has_data ? (transfer->length - residual) : 0;
		if (has_data && data_dir_in) {
			memcpy(transfer->buffer, bounce, transfer->actual_length);
		}
		if (bounce) kfree_dma(bounce);
		return 0;
	}

	transfer->actual_length = 0;
	printf_serial("[xHCI][ERROR] Control transfer failed, completion code: %u\r\n", comp_code);

	// Endpoint is very likely Halted now
	// It will not accept new doorbell rings until reset.
	if (!xhci_recover_halted_endpoint(hc, xdev, xep)) {
		printf_serial("[xHCI][ERROR] Failed to recover endpoint after transfer error (slot=%u dci=%u). Endpoint is likely unusable.\r\n", xdev->slot_id, xep->dci);
		printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "[xHCI][ERROR] Failed to recover endpoint after transfer error (slot=%u dci=%u). Endpoint is likely unusable.\r\n", xdev->slot_id, xep->dci);
		if (bounce) kfree_dma(bounce);
		return XHCI_TX_ERR_RECOVERY_FAILED;
	}

	if (bounce) kfree_dma(bounce);

	// Endpoint recovered successfully, but the transfer itself still failed
	return XHCI_TX_ERR_COMPLETION_BASE - (int) comp_code;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// HCD ops table
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

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

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Probe / attach / detach
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

int xhci_probe(wallos_device_t* dev) {
	if (!dev) return -1;

	// zero indicates that this device does belong to this driver
	// im too lazy to properly check that we can interface with this so I wont right now
	return 0;
}

void xhci_init_regs(xhci_controller_t* hc, uintptr_t base) {
	hc->mmio_base = base;

	hc->cap = (volatile xhci_cap_regs_t*) base;
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

	uint16_t pci_cmd = pci_config_read16(bus, slot, func, 0x04);
	pci_cmd |= BIT(1) | BIT(2); // Memory Space Enable, Bus Master Enable
	pci_config_write16(bus, slot, func, 0x04, pci_cmd);

	// let this line change serve as warning to always initialize memory to zero...
	// I spent 4 hours debugging an issue caused by not zeroing this...
	dev->driver_data = kcalloc(1, sizeof(xhci_controller_t));
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
	// dboff = FIELD_GET(GENMASK(31, 2), dboff);
	dboff &= GENMASK(31, 2);
	printf_serial("[xHCI] DBOFF = 0x%x\r\n", dboff);
	hc->doorbell = (volatile xhci_doorbell_regs_t*) ((uintptr_t) hc->mmio_base + dboff);

	/* RTSOFF */
	uint32_t rtsoff = mmio_read32(&hc->cap->rtsoff);
	// rtsoff = FIELD_GET(GENMASK(31, 5), rtsoff);
	rtsoff &= GENMASK(31, 5);
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

	if (max_scratchpad_bufs > 0) {
		uint32_t pagesize_reg = mmio_read32(&hc->op->pagesize);
		// pagesize zero in this context would be illegal anyway
		if (pagesize_reg == 0) return; // TODO: need cleanup
		// PAGESIZE is a bitmap
		// bit n set means the page size is 2^(n+12) bytes
		uint32_t page_size = 4096u << __builtin_ctz(pagesize_reg);

		uintptr_t sp_array_phys;
		uint64_t* sp_array = (uint64_t*) kalloc_dma(max_scratchpad_bufs * sizeof(uint64_t), DMA_ALIGN_64 | DMA_ZONE_ANY, PDE_FLAGS_UC_2MB, &sp_array_phys);
		if (!sp_array) {
			printf_serial("[xHCI][ERROR] Failed to allocate Scratchpad Buffer Array... \r\n");
			return;
		}

		for (uint16_t i = 0; i < max_scratchpad_bufs; i++) {
			uintptr_t buf_phys;
			void* buf = kalloc_dma(page_size, DMA_ALIGN_64 | DMA_ZONE_ANY, PDE_FLAGS_UC_2MB, &buf_phys);
			if (!buf) {
				printf_serial("[xHCI][ERROR] Failed to allocate scratchpad buffer %u... \r\n", i);
				return;
			}
			memset(buf, 0, page_size);
			sp_array[i] = (uint64_t) buf_phys;
		}

		hc->dcbaa[0] = (uint64_t) sp_array_phys;
		printf_serial("[xHCI] Scratchpad Buffer Array: %u buffers of %u bytes, DCBAA[0] = %p\r\n", max_scratchpad_bufs, page_size, (void*) sp_array_phys);
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

	crcr_value = hc->command_ring.trbs_phys & GENMASK(63, 6);
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

	// everything *should* be set up
	// we can set the R/S bit to 1 in usbsts

	uint32_t usbcmd = mmio_read32(&hc->op->usbcmd);
	FIELD_WRITE(usbcmd, GENMASK(0, 0), 1);
	mmio_write32(&hc->op->usbcmd, usbcmd);

	while (FIELD_GET(GENMASK(0, 0), mmio_read32(&hc->op->usbsts))) {
		cpu_pause();
	}

	// Just pause for like 10ms to let the controller actually full come up.
	// I've found we need this delay on real hardware, otherwise the controller will report incorrect portsc values
	// I assume it just needs a bit to settle internal states, 10ms is basically nothing perceptible anyway. 
	xhci_delay_us(10 * 1000);

	usb_hcd_t* hcd = (usb_hcd_t*) kcalloc(1, sizeof(usb_hcd_t));
	hcd->ops = &xhci_ops;
	hcd->device = dev;
	hcd->type = USB_HCD_XHCI;
	hcd->hcd_data = (void*) hc; // we just store the entire xhci_controller_t here. This is the same pointer that is in dev->driver_data for the HCD.

	usb_hcd_register(hcd);
}

void xhci_detach(wallos_device_t* dev) {
	(void) dev;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Driver registration
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

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