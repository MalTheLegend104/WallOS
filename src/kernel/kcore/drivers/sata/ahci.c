#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include <cpu_io.h>
#include <endian_bits.h>

#include <system/timer.h>

#include <drivers/serial.h>
#include <drivers/pci.h>
#include <drivers/sata/ahci.h>

#include <memory/virtual_mem.h>
#include <memory/kernel_alloc.h>


#define AHCI_MMIO_FLAGS  (BIT_PRESENT | BIT_WRITE | BIT_PCD | BIT_SIZE)

// Forward declares to prevent having to put things in the header that shouldn't be there.

wallos_driver_t ahci_driver;
WDM_DriveHandle ahci_wdm_register_port(ahci_port_t* port, uint32_t slot_count);

// Oh look, it's this stupid way of doing this again.
// Defined in yet another spot....
// We really need to set up a udelay() function...
static inline void ahci_udelay(uint32_t us) {
	busy_wait_us(us);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Inline helpers. Make code significantly easier to read.
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static inline uint32_t hba_read(ahci_ctrl_t* ctrl, uint32_t reg) { return mmio_read32((volatile void*) (ctrl->abar + reg)); }
static inline void hba_write(ahci_ctrl_t* ctrl, uint32_t reg, uint32_t val) { mmio_write32((volatile void*) (ctrl->abar + reg), val); }

static inline uint32_t port_read(ahci_port_t* port, uint32_t reg) { return mmio_read32((volatile void*) (port->port_base + reg)); }
static inline void port_write(ahci_port_t* port, uint32_t reg, uint32_t val) { mmio_write32((volatile void*) (port->port_base + reg), val); }

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Port Start / Stop
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

void port_stop_cmd(ahci_port_t* port) {
	uint32_t cmd = port_read(port, AHCI_PXCMD);
	cmd &= ~(AHCI_CMD_ST | AHCI_CMD_FRE);
	port_write(port, AHCI_PXCMD, cmd);

	// Wait for CR and FR to clear (up to ~500ms)
	uint32_t timeout = 50000;
	while (timeout--) {
		cmd = port_read(port, AHCI_PXCMD);
		if (!(cmd & (AHCI_CMD_FR | AHCI_CMD_CR))) break;
		ahci_udelay(10);
	}
}

void port_start_cmd(ahci_port_t* port) {
	// Wait until CR is clear before setting ST
	uint32_t timeout = 50000;
	while (timeout--) {
		if (!(port_read(port, AHCI_PXCMD) & AHCI_CMD_CR)) break;
		ahci_udelay(10);
	}

	uint32_t cmd = port_read(port, AHCI_PXCMD);
	cmd |= AHCI_CMD_FRE | AHCI_CMD_ST;
	port_write(port, AHCI_PXCMD, cmd);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// HBA Reset
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

int hba_reset(ahci_ctrl_t* ctrl) {
	uint32_t ghc = hba_read(ctrl, AHCI_REG_GHC);
	// Ensure AHCI mode is on before resetting
	hba_write(ctrl, AHCI_REG_GHC, ghc | AHCI_GHC_AE);

	hba_write(ctrl, AHCI_REG_GHC, hba_read(ctrl, AHCI_REG_GHC) | AHCI_GHC_HR);

	// AHCI spec states that HR must self-clear within 1 second
	uint32_t timeout = 100000;
	while (timeout--) {
		if (!(hba_read(ctrl, AHCI_REG_GHC) & AHCI_GHC_HR)) break;
		ahci_udelay(10);
	}

	if (hba_read(ctrl, AHCI_REG_GHC) & AHCI_GHC_HR) {
		printf_serial("[AHCI] HBA reset timed out\r\n");
		return -1;
	}

	// Re-enable AHCI mode and interrupts
	hba_write(ctrl, AHCI_REG_GHC, AHCI_GHC_AE | AHCI_GHC_IE);
	return 0;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Port Initialisation
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

void port_rebase(ahci_port_t* port) {
	port_stop_cmd(port);

	ahci_port_mem_t* mem = port->mem;
	uintptr_t phys = virt_to_phys((uintptr_t) mem);

	// Command list base (1KB, 1KB aligned)
	port_write(port, AHCI_PXCLB, (uint32_t) (phys & 0xFFFFFFFF));
	port_write(port, AHCI_PXCLBU, (uint32_t) (phys >> 32));

	// FIS base (256 bytes, 256 byte aligned)
	uintptr_t fis_phys = phys + offsetof(ahci_port_mem_t, fis_buf);
	port_write(port, AHCI_PXFB, (uint32_t) (fis_phys & 0xFFFFFFFF));
	port_write(port, AHCI_PXFBU, (uint32_t) (fis_phys >> 32));

	// Point each command header's CTBA at our single shared command table
	uintptr_t ct_phys = phys + offsetof(ahci_port_mem_t, cmd_table);
	for (int i = 0; i < 32; i++) {
		mem->cmd_list[i].prdtl = AHCI_MAX_PRD;
		mem->cmd_list[i].ctba = (uint32_t) (ct_phys & 0xFFFFFFFF);
		mem->cmd_list[i].ctbau = (uint32_t) (ct_phys >> 32);
	}

	// Clear error and interrupt status
	port_write(port, AHCI_PXSERR, 0xFFFFFFFF);
	port_write(port, AHCI_PXIS, 0xFFFFFFFF);

	// Enable specific interrupts: D2H FIS, PIO setup, DMA setup, task file error
	port_write(port, AHCI_PXIE, 0xFDC000FF);

	port_start_cmd(port);
}

// ------------------------------------------------------------------------------------------------
// Device type detection
// ------------------------------------------------------------------------------------------------

ahci_dev_type_t detect_port_type(ahci_port_t* port) {
	uint32_t ssts = port_read(port, AHCI_PXSSTS);
	uint8_t det = ssts & AHCI_SSTS_DET_MASK;

	if (det != AHCI_SSTS_DET_PRESENT) return AHCI_DEV_NONE;

	// Enable FIS Receive Engine so the HBA can capture the signature
	uint32_t cmd = port_read(port, AHCI_PXCMD);
	cmd |= AHCI_CMD_FRE;
	port_write(port, AHCI_PXCMD, cmd);

	// Wait for the drive to send the initial FIS (up to 1 sec)
	// TFD Bit 7 is BSY, Bit 3 is DRQ. Both should be 0 when ready.
	uint32_t timeout = 100000;
	while (timeout--) {
		uint32_t tfd = port_read(port, AHCI_PXTFD);
		if (!(tfd & (AHCI_TFD_BSY | AHCI_TFD_DRQ))) break;
		ahci_udelay(10);
	}

	uint32_t sig = port_read(port, AHCI_PXSIG);

	// Debug log to see if signature changed from 0xFFFFFFFF
	// printf_serial("[AHCI] port %u final sig: 0x%08x\n", port->port_idx, sig);

	switch (sig) {
		case AHCI_SIG_SATA:  return AHCI_DEV_SATA;
		case AHCI_SIG_ATAPI: return AHCI_DEV_SATAPI;
		default:             return AHCI_DEV_NONE;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Command Slot
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Find a free command slot (returns -1 if none available)
int find_free_slot(ahci_port_t* port, uint32_t slot_count) {
	uint32_t sact = port_read(port, AHCI_PXSACT);
	uint32_t ci = port_read(port, AHCI_PXCI);
	uint32_t busy = sact | ci;

	for (uint32_t i = 0; i < slot_count; i++) {
		if (!(busy & (1U << i))) return (int) i;
	}
	return -1;
}

// Wait for a command slot to complete by polling
int wait_for_slot(ahci_port_t* port, int slot, uint32_t timeout_ms) {
	uint32_t ticks = timeout_ms * 100; // 10us granularity
	while (ticks--) {
		uint32_t tfd = port_read(port, AHCI_PXTFD);
		if (tfd & AHCI_TFD_ERR) {
			printf_serial("[AHCI][ERROR] port %u task file error (TFD=0x%08x)\r\n", port->port_idx, tfd);
			return -1;
		}

		if (!(port_read(port, AHCI_PXCI) & (1U << slot))) return 0;

		ahci_udelay(10);
	}

	// This isn't particularly fatal, so it's only listed as a warning.
	// This driver *does* treat it as fatal though.
	printf_serial("[AHCI][WARN] port %u command slot %d timed out\r\n", port->port_idx, slot);
	return -1;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Command Dispatch
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/*
 * Issue a single ATA command with one optional data buffer.
 * buf may be NULL (e.g. for FLUSH).
 * is_write: 1 = H->D (write), 0 = D->H (read).
 * Returns 0 on success, -1 on error.
 */
int ahci_issue_ata(ahci_port_t* port, uint32_t slot_count, ahci_fis_h2d_t* fis, void* buf, uint32_t byte_count, int is_write) {
	int slot = find_free_slot(port, slot_count);
	if (slot < 0) {
		printf_serial("[AHCI] port %u no free command slots\r\n", port->port_idx);
		return -1;
	}

	ahci_port_mem_t* mem = port->mem;

	// Command Header
	ahci_cmd_header_t* hdr = &mem->cmd_list[slot];
	memset(hdr, 0, sizeof(*hdr));
	hdr->flags = (uint16_t) ((sizeof(ahci_fis_h2d_t) / 4) & AHCI_CMD_HDR_FLAG_CFL_MASK);
	hdr->flags |= is_write ? AHCI_CMD_HDR_FLAG_WRITE : 0;
	hdr->prdtl = (buf && byte_count) ? 1 : 0;
	hdr->prdbc = 0;

	// CTBA was set during rebase
	// we reuse the single cmd_table
	uintptr_t ct_phys = virt_to_phys((uintptr_t) mem->cmd_table);
	hdr->ctba = (uint32_t) (ct_phys & 0xFFFFFFFF);
	hdr->ctbau = (uint32_t) (ct_phys >> 32);

	// Command Table: CFIS
	uint8_t* ct = mem->cmd_table;
	memset(ct, 0, AHCI_CMDT_TOTAL_SIZE(AHCI_MAX_PRD));
	memcpy(ct + AHCI_CMDT_CFIS_OFF, fis, sizeof(ahci_fis_h2d_t));

	// Command Table: PRDT
	if (buf && byte_count) {
		ahci_prd_t* prd = (ahci_prd_t*) (ct + AHCI_CMDT_PRDT_OFF);
		uintptr_t   dba = virt_to_phys((uintptr_t) buf);
		prd->dba = (uint32_t) (dba & 0xFFFFFFFF);
		prd->dbau = (uint32_t) (dba >> 32);
		prd->reserved = 0;
		prd->dbc = (byte_count - 1) & 0x3FFFFF; // dbc is (N-1)
	}

	// Clear any stale errors
	port_write(port, AHCI_PXSERR, 0xFFFFFFFF);
	port_write(port, AHCI_PXIS, 0xFFFFFFFF);

	// Issue command
	port_write(port, AHCI_PXCI, 1U << slot);

	return wait_for_slot(port, slot, 5000 /* 5s timeout */);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// IDENTIFY
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// ATA IDENTIFY response is 512 bytes of 16-bit words in a specific layout
#define ATA_IDENT_SERIAL_OFF  10   // words 10-19
#define ATA_IDENT_MODEL_OFF   27   // words 27-46
#define ATA_IDENT_LBA48_OFF   83   // word 83. bit 10 = LBA48 support
#define ATA_IDENT_LBASECT_OFF 100  // words 100-103. 64-bit sector count (LBA48)

void swap_ata_string(uint16_t* words, char* out, int word_count) {
	for (int i = 0; i < word_count; i++) {
		out[i * 2] = (char) (words[i] >> 8);
		out[i * 2 + 1] = (char) (words[i] & 0xFF);
	}
	out[word_count * 2] = '\0';
	// Trim trailing spaces
	for (int i = word_count * 2 - 1; i >= 0 && out[i] == ' '; i--) out[i] = '\0';
}

int port_identify(ahci_port_t* port, uint32_t slot_count) {
	// Using a stack buffer because it's easier
	// fine for polling since it's synchronous
	static uint16_t id_buf[256]; // 512 bytes
	memset(id_buf, 0, sizeof(id_buf));

	ahci_fis_h2d_t fis;
	memset(&fis, 0, sizeof(fis));
	fis.fis_type = AHCI_FIS_TYPE_H2D;
	fis.pmport_c = AHCI_FIS_H2D_C_BIT;
	fis.command = ATA_CMD_IDENTIFY;
	fis.device = 0;
	fis.count = 0; // Not used for IDENTIFY

	if (ahci_issue_ata(port, slot_count, &fis, id_buf, 512, 0) != 0) {
		printf_serial("\r\n[AHCI][ERROR] port %u IDENTIFY failed\r\n", port->port_idx);
		return -1;
	}

	// LBA48 sector count
	if (id_buf[ATA_IDENT_LBA48_OFF] & (1U << 10)) {
		port->sector_count =
			((uint64_t) id_buf[ATA_IDENT_LBASECT_OFF + 3] << 48) |
			((uint64_t) id_buf[ATA_IDENT_LBASECT_OFF + 2] << 32) |
			((uint64_t) id_buf[ATA_IDENT_LBASECT_OFF + 1] << 16) |
			((uint64_t) id_buf[ATA_IDENT_LBASECT_OFF]);
	} else {
		port->sector_count = ((uint32_t) id_buf[61] << 16) | id_buf[60]; // LBA28 fallback
	}

	port->sector_size = 512; // 512 is what most drives have. word 106 can refine this

	swap_ata_string(&id_buf[ATA_IDENT_MODEL_OFF], port->model, 20);
	swap_ata_string(&id_buf[ATA_IDENT_SERIAL_OFF], port->serial, 10);

	printf_serial("(model='%s' serial='%s' sectors=%llu) ",
		port->model, port->serial,
		(unsigned long long)port->sector_count);
	return 0;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// AHCI Write/Read
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

int ahci_read_sectors(ahci_port_t* port, uint64_t lba, uint32_t count, void* buf) {
	if (!port || !port->present || !buf || count == 0) return -1;

	// We currently only handle a single PRD worth of data
	uint32_t bytes = count * port->sector_size;
	if (bytes > AHCI_PRD_MAX_BYTES) {
		printf_serial("[AHCI] read too large (%u bytes, max %u)\r\n", bytes, AHCI_PRD_MAX_BYTES);
		return -1;
	}

	ahci_fis_h2d_t fis;
	memset(&fis, 0, sizeof(fis));
	fis.fis_type = AHCI_FIS_TYPE_H2D;
	fis.pmport_c = AHCI_FIS_H2D_C_BIT;
	fis.command = ATA_CMD_READ_DMA_EXT;
	fis.device = ATA_DEV_LBA;
	fis.lba0 = (uint8_t) (lba);
	fis.lba1 = (uint8_t) (lba >> 8);
	fis.lba2 = (uint8_t) (lba >> 16);
	fis.lba3 = (uint8_t) (lba >> 24);
	fis.lba4 = (uint8_t) (lba >> 32);
	fis.lba5 = (uint8_t) (lba >> 40);
	fis.count = (uint16_t) count;

	// Find slot count from the parent controller
	// We store slot_count in ahci_ctrl_t, reach it via the device if needed.
	// For now, use a safe conservative of 1 (always works).
	return ahci_issue_ata(port, 1, &fis, buf, bytes, 0);
}

int ahci_write_sectors(ahci_port_t* port, uint64_t lba, uint32_t count, const void* buf) {
	if (!port || !port->present || !buf || count == 0) return -1;

	uint32_t bytes = count * port->sector_size;
	if (bytes > AHCI_PRD_MAX_BYTES) {
		printf_serial("[AHCI] write too large (%u bytes, max %u)\r\n", bytes, AHCI_PRD_MAX_BYTES);
		return -1;
	}

	ahci_fis_h2d_t fis;
	memset(&fis, 0, sizeof(fis));
	fis.fis_type = AHCI_FIS_TYPE_H2D;
	fis.pmport_c = AHCI_FIS_H2D_C_BIT;
	fis.command = ATA_CMD_WRITE_DMA_EXT;
	fis.device = ATA_DEV_LBA;
	fis.lba0 = (uint8_t) (lba);
	fis.lba1 = (uint8_t) (lba >> 8);
	fis.lba2 = (uint8_t) (lba >> 16);
	fis.lba3 = (uint8_t) (lba >> 24);
	fis.lba4 = (uint8_t) (lba >> 32);
	fis.lba5 = (uint8_t) (lba >> 40);
	fis.count = (uint16_t) count;

	return ahci_issue_ata(port, 1, &fis, (void*) (uintptr_t) buf, bytes, 1);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Driver Ops: probe / attach / detach
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

int ahci_probe(wallos_device_t* dev) {
	printf_serial("[AHCI] Probing device: %s\r\n", dev->name);
	if (!dev) return -1;

	/* We don't need to do anything special in probe.
	 * Here we are simply checking that the controller is, in fact, an AHCI controller.
	 * Probe is much more useful for "chained" drivers (I designed the driver manager with USB in mind),
	 * AHCI is pretty straight forward, and it handles everything it needs on it's own.
	 */

	// It should be basically impossible for anything below to return anything other than 0.
	// It would require the driver manager to make a colossal screw up. 

	// Must be a PCI device with AHCI controller flag
	if (!DEV_INT_HAS(dev->interfaces, DEV_INT_PCI)) return -1;
	if (!DEV_INT_HAS(dev->interfaces, DEV_INT_AHCI)) return -1;

	// Double-check via PCI class/subclass/prog-if
	uint8_t bus = dev->location.pci.bus;
	uint8_t slot = dev->location.pci.slot;
	uint8_t func = dev->location.pci.function;

	uint32_t classinfo = pci_config_read32(bus, slot, func, 0x08);
	uint8_t base_class = (classinfo >> 24) & 0xFF;
	uint8_t sub_class = (classinfo >> 16) & 0xFF;
	uint8_t prog_if = (classinfo >> 8) & 0xFF;

	if (base_class != AHCI_PCI_CLASS ||
		sub_class != AHCI_PCI_SUBCLASS ||
		prog_if != AHCI_PCI_PROG_IF) {
		return -1;
	}

	return 0; // Accept this device
}

void ahci_attach(wallos_device_t* dev) {
	if (!dev) return;

	uint8_t bus = dev->location.pci.bus;
	uint8_t slot = dev->location.pci.slot;
	uint8_t func = dev->location.pci.function;

	// Enable PCI bus-mastering and memory space decoding
	pci_set_bus_master(bus, slot, func, 1);
	pci_set_mem_space(bus, slot, func, 1);

	// Read ABAR (BAR5 for AHCI)
	// pci_read_bar returns the *physical* address (this caused me much pain in debugging before I realized it)
	int is_io, is_64;
	uintptr_t abar_phys = pci_read_bar(bus, slot, func, 5, &is_io, &is_64);
	if (!abar_phys || is_io) {
		printf_serial("[AHCI] BAR5 is not a valid MMIO BAR\r\n");
		return;
	}

	// Wee need to map the MMIO region into kernel space.
	// This handles page alignment and page boundary splitting for us.
	uintptr_t abar = mapKernelLocationWithFlags(abar_phys, 0xC00, AHCI_MMIO_FLAGS);
	if (!abar) {
		printf_serial("[AHCI][ERROR] failed to map ABAR phys=0x%lx\r\n", (unsigned long) abar_phys);
		return;
	}

	// printf_serial("[AHCI] ABAR phys=0x%lx -> virt=0x%lx\r\n", (unsigned long) abar_phys, (unsigned long) abar);

	ahci_ctrl_t* ctrl = (ahci_ctrl_t*) kalloc(sizeof(ahci_ctrl_t));
	if (!ctrl) {
		printf_serial("[AHCI][ERROR] out of memory for controller state\r\n");
		return;
	}
	memset(ctrl, 0, sizeof(*ctrl));
	ctrl->abar = abar;
	ctrl->abar_phys = abar_phys;

	if (hba_reset(ctrl) != 0) {
		kfree(ctrl);
		return;
	}

	printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "[AHCI] Attaching device %s...\n", dev->name);

	// Read capabilities
	uint32_t cap = hba_read(ctrl, AHCI_REG_CAP);
	ctrl->port_count = (cap & AHCI_CAP_NP_MASK) + 1;
	ctrl->slot_count = ((cap & AHCI_CAP_NCS_MASK) >> AHCI_CAP_NCS_SHIFT) + 1;
	ctrl->supports_64bit = (cap & AHCI_CAP_S64A) ? 1 : 0;

	uint32_t pi = hba_read(ctrl, AHCI_REG_PI);

	printf_serial("[AHCI] HBA at 0x%lx, %u port(s) implemented, %u cmd slot(s), 64-bit=%d\r\n",
		(unsigned long) abar, ctrl->port_count, ctrl->slot_count, ctrl->supports_64bit);

	uint32_t version = hba_read(ctrl, AHCI_REG_VS);
	printf_serial("[AHCI] AHCI version %u.%u\r\n", (version >> 16) & 0xFFFF, version & 0xFFFF);

	printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "\t%u port(s) implemented, %u cmd slot(s), 64-bit=%d\n", ctrl->port_count, ctrl->slot_count, ctrl->supports_64bit);
	printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "\tAHCI Version %u.%u\n", (version >> 16) & 0xFFFF, version & 0xFFFF);

	// Initialise each implemented port
	for (uint32_t i = 0; i < 32; i++) {
		if (!(pi & (1U << i))) {
			// This is really useful for debugging but INCREDIBLY annoying with multiple AHCI controllers
			// printf_serial("[AHCI] port %u not implemented (PI bit not set)\r\n", i);
			continue;
		}
		printf_serial("[AHCI] Probing port %u... ", i);

		ahci_port_t* port = &ctrl->ports[i];
		port->port_idx = i;
		port->port_base = abar + AHCI_PORT_BASE(i);


		// uint32_t ssts = port_read(port, AHCI_PXSSTS);
		// uint32_t sig = port_read(port, AHCI_PXSIG);
		// uint32_t tfd = port_read(port, AHCI_PXTFD);
		// These are useful to inspect the port status directly
		// printf_serial("[AHCI] port %u base=0x%lx\r\n", i, (unsigned long) port->port_base);
		// printf_serial("[AHCI] port %u SSTS=0x%08x, SIG=0x%08x, TFD=0x%08x\n", i, ssts, sig, tfd);

		ahci_dev_type_t type = detect_port_type(port);
		// printf_serial("[AHCI] port %u detected type=%d\r\n", i, type);

		if (type == AHCI_DEV_NONE) {
			printf_serial("empty.\r\n");
			continue;
		}
		if (type == AHCI_DEV_SEMB) {
			printf_serial("is SEMB (ignored).\r\n");
			continue;
		}
		if (type == AHCI_DEV_PM) {
			printf_serial("is port multiplier (ignored).\r\n");
			continue;
		}
		if (type == AHCI_DEV_SATA) {
			printf_serial("regular SATA device.\r\n");
			printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "\tPort %u is regular SATA device.\n", i);
		}
		if (type == AHCI_DEV_SATAPI) {
			printf_serial("SATAPI device.\r\n");
			printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "\tPort %u is SATAPI device.\n", i);
		}

		port->type = type;
		port->present = 1;

		// Allocate DMA memory for command list + FIS + command table
		uintptr_t phys_unused;
		port->mem = (ahci_port_mem_t*) kalloc_dma(sizeof(ahci_port_mem_t), DMA_ZONE_ANY, 0, &phys_unused);
		if (!port->mem) {
			printf_serial("[AHCI][ERROR] port %u: DMA alloc failed\r\n", i);
			port->present = 0;
			continue;
		}

		// printf_serial("[AHCI] port %u: mem virt=%p phys=0x%lx size=0x%lx\r\n", i, port->mem, (unsigned long) phys_unused, (unsigned long) sizeof(ahci_port_mem_t));

		memset(port->mem, 0, sizeof(ahci_port_mem_t));

		printf_serial("[AHCI] Rebasing port %u... ", i);
		port_rebase(port);
		printf_serial("done.\r\n", i);

		if (type == AHCI_DEV_SATA) {
			printf_serial("[AHCI] Issuing IDENTIFY on port %u... ", i);
			int res = port_identify(port, ctrl->slot_count);
			if (res != 0) { printf_serial("Failed identify.\r\n"); continue; }
			printf_serial("complete.\r\n", i);

			WDM_DriveHandle wdm_handle = ahci_wdm_register_port(port, ctrl->slot_count);
			if (!wdm_handle) {
				printf_serial("[AHCI][WARN] port %u: WDM registration failed, " "drive will not be accessible\r\n", i);
			}
			// Store the handle in the port so ahci_detach can unregister it
			port->wdm_handle = wdm_handle;


			printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "\tModel: %s (Sectors: %llu, Size: %llu)\n", port->model, port->sector_count, port->sector_count * port->sector_size);

			// TODO: This is quick and dirty, should probably be done dynamically.
			// On the other hand, I don't particularly see us having this many drives...
			// This also does take into account different controllers, so one drive will be 
			// /pci/ahci0/sataX
			// While another will be 
			// /pci/ahci1/sataX
			const char* names[] = {
				"sata0","sata1","sata2","sata3","sata4","sata5",
				"sata6","sata7","sata8","sata9","sata10","sata11",
				"sata12","sata13","sata14","sata15","sata16","sata17",
				"sata18","sata19","sata20","sata21","sata22","sata23",
				"sata24","sata25","sata26","sata27","sata28","sata29",
				"sata30","sata31"
			};

			device_interface_flags_t child_flags = DEV_INT_AHCI | DEV_INT_MMIO;
			wallos_device_t* child = create_device(child_flags, names[i]);
			if (child) {
				printf_serial("[AHCI] Registering child device on port %u: %s\r\n", i, names[i]);

				child->parent = dev;
				child->driver_data = port;
				child->next_sibling = dev->first_child;
				dev->first_child = child;
				register_device(child);

				// This takes the load of having to deal with partitions off of us. 
				// In theory, this takes care of everything we need to take care of, including registering the device.
				WDM_Status stat = WDM_ScanAndRegisterPartitions(wdm_handle, child);
				if (stat != 0) printf_serial("[AHCI][WARN] stat = %d\r\n", stat);
			} else {
				printf_serial("[AHCI][ERROR] port %u: failed to create child device\r\n", i);
			}
		}
	}

	dev->driver_data = ctrl;
	dev->bound_driver = &ahci_driver;

	printf_serial("[AHCI] AHCI controller (%s) has been attached.\r\n", dev->name);
	printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "[AHCI] Finished attaching controller (%s).\n", dev->name);
}

void remove_ahci_dev_tree(wallos_device_t* root) {
	if (!root) return;

	// Recursively tear down all children first
	wallos_device_t* child = root->first_child;
	while (child) {
		wallos_device_t* next = child->next_sibling;
		remove_ahci_dev_tree(child);
		child = next;
	}

	if (root->name) {
		kfree((void*) root->name);
		root->name = NULL;
	}

	remove_device(root);
}

void ahci_detach(wallos_device_t* dev) {
	// TODO: whenever we add the partition support into WDM, we need to make sure to properly clean up the child partition devices.
	if (!dev || !dev->driver_data) return;

	ahci_ctrl_t* ctrl = (ahci_ctrl_t*) dev->driver_data;

	for (uint32_t i = 0; i < 32; i++) {
		ahci_port_t* port = &ctrl->ports[i];
		if (!port->present) continue;

		if (port->wdm_handle) {
			// this will recursively clean up the child partition devices
			WDM_Unregister(port->wdm_handle);
			port->wdm_handle = NULL;
		}

		port_stop_cmd(port);

		if (port->mem) {
			kfree_dma(port->mem);
			port->mem = NULL;
		}
	}

	// Remove child devices from the tree
	wallos_device_t* child = dev->first_child;
	while (child) {
		wallos_device_t* next = child->next_sibling;
		remove_ahci_dev_tree(child);
		child = next;
	}
	dev->first_child = NULL;

	kfree(ctrl);
	dev->driver_data = NULL;
	dev->bound_driver = NULL;

	printf_serial("[AHCI] controller detached\r\n");
}


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Driver Registration
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

wallos_driver_t ahci_driver = {
	.name = "ahci",
	.match_flags = DEV_INT_PCI | DEV_INT_AHCI,
	.match_mask = DEV_INT_PCI | DEV_INT_AHCI,
	// We don't care about vendor or device, AHCI *should* be universal
	.vendor_id = 0x0000,
	.device_id = 0x0000,
	.ops = {
		.probe = ahci_probe,
		.attach = ahci_attach,
		.detach = ahci_detach,
	},
	.next = NULL
};

void ahci_register_driver(void) {
	dm_error_t err = dm_register_driver(&ahci_driver);
	if (err != DM_ERROR_NONE) {
		printf_serial("[AHCI] failed to register driver (err=%d)\r\n", (int) err);
	} else {
		printf_serial("[AHCI] driver registered\r\n");
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// WDM Glue
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
typedef struct {
	ahci_port_t* port;
	uint32_t slot_count; // Copied from ctrl so the vtable doesn't need ctrl
} ahci_wdm_ctx_t;

WDM_Status ahci_wdm_read(void* ctx, WDM_LBA lba, uint32_t count, void* buf, WDM_IOFlags flags) {
	(void) flags; // Polling only for now, we dont care about the DMA flag
	ahci_wdm_ctx_t* c = (ahci_wdm_ctx_t*) ctx;
	int r = ahci_read_sectors(c->port, lba, count, buf);
	if (r == 0) return WDM_OK;
	return WDM_ERR_IO;
}

WDM_Status ahci_wdm_write(void* ctx, WDM_LBA lba, uint32_t count, const void* buf, WDM_IOFlags flags) {
	ahci_wdm_ctx_t* c = (ahci_wdm_ctx_t*) ctx;

	int r = ahci_write_sectors(c->port, lba, count, buf);
	if (r != 0) return WDM_ERR_IO;

	// If the caller wants a synchronous commit, issue a cache flush immediately
	if (flags & WDM_FLAG_SYNC) {
		ahci_fis_h2d_t fis;
		memset(&fis, 0, sizeof(fis));
		fis.fis_type = AHCI_FIS_TYPE_H2D;
		fis.pmport_c = AHCI_FIS_H2D_C_BIT;
		fis.command = ATA_CMD_FLUSH_CACHE_EXT;
		fis.device = 0;
		if (ahci_issue_ata(c->port, c->slot_count, &fis, NULL, 0, 0) != 0) return WDM_ERR_IO;
	}

	return WDM_OK;
}

WDM_Status ahci_wdm_flush(void* ctx) {
	ahci_wdm_ctx_t* c = (ahci_wdm_ctx_t*) ctx;

	ahci_fis_h2d_t fis;
	memset(&fis, 0, sizeof(fis));
	fis.fis_type = AHCI_FIS_TYPE_H2D;
	fis.pmport_c = AHCI_FIS_H2D_C_BIT;
	fis.command = ATA_CMD_FLUSH_CACHE_EXT;
	fis.device = 0;

	int r = ahci_issue_ata(c->port, c->slot_count, &fis, NULL, 0, 0);
	return (r == 0) ? WDM_OK : WDM_ERR_IO;
}

void ahci_wdm_on_detach(void* ctx) {
	// The port memory is owned by ahci_ctrl_t and freed in ahci_detach.
	// All we own here is the context struct itself.
	kfree(ctx);
}

static const WDM_DriverOps ahci_wdm_ops = {
	.read = ahci_wdm_read,
	.write = ahci_wdm_write,
	.flush = ahci_wdm_flush,
	.trim = NULL, // HDDs don't support TRIM
	.on_attach = NULL, // Nothing extra needed at registration time
	.on_detach = ahci_wdm_on_detach,
};

// Called from ahci_attach after a port is identified and its child device registered.
// Returns the WDM handle on success, NULL on failure.
WDM_DriveHandle ahci_wdm_register_port(ahci_port_t* port, uint32_t slot_count) {
	ahci_wdm_ctx_t* ctx = (ahci_wdm_ctx_t*) kalloc(sizeof(ahci_wdm_ctx_t));
	if (!ctx) {
		printf_serial("[AHCI][ERROR] port %u: failed to alloc WDM context\r\n", port->port_idx);
		return NULL;
	}
	ctx->port = port;
	ctx->slot_count = slot_count;

	WDM_DriveInfo info;
	memset(&info, 0, sizeof(info));
	info.sector_count = port->sector_count;
	info.sector_size = port->sector_size;
	info.physical_sector = port->sector_size; // Refine with word 106 if needed
	info.optimal_xfer = 128; // 64K at 512B/sector, safe conservative estimate
	info.removable = false;
	info.read_only = false;
	info.dma_capable = true;
	strncpy(info.model, port->model, sizeof(info.model) - 1);
	strncpy(info.serial, port->serial, sizeof(info.serial) - 1);

	WDM_DriveHandle handle = NULL;
	WDM_Status st = WDM_Register(&ahci_wdm_ops, ctx, &info, &handle);
	if (st != WDM_OK) {
		printf_serial("[AHCI][ERROR] port %u: WDM_Register failed (%d)\r\n", port->port_idx, (int) st);
		kfree(ctx);
		return NULL;
	}

	printf_serial("[AHCI] port %u registered with WDM\r\n", port->port_idx);
	return handle;
}