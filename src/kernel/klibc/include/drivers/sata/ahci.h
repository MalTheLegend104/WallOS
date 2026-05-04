#ifndef WALLOS_AHCI_H
#define WALLOS_AHCI_H

#include <stdint.h>
#include <stddef.h>
#include <drivers/driver_manager.h>
#include <device/device_manager.h>

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------------------------------------------
// AHCI PCI Identity
// ------------------------------------------------------------------------------------------------
#define AHCI_PCI_CLASS     0x01
#define AHCI_PCI_SUBCLASS  0x06
#define AHCI_PCI_PROG_IF   0x01

// ------------------------------------------------------------------------------------------------
// AHCI HBA Memory-Mapped Register Offsets (Generic Host Control)
// ------------------------------------------------------------------------------------------------
#define AHCI_REG_CAP       0x00  // Host Capabilities
#define AHCI_REG_GHC       0x04  // Global Host Control
#define AHCI_REG_IS        0x08  // Interrupt Status
#define AHCI_REG_PI        0x0C  // Ports Implemented
#define AHCI_REG_VS        0x10  // Version
#define AHCI_REG_CAP2      0x24  // Host Capabilities Extended

// GHC bit flags
#define AHCI_GHC_HR        (1U << 0)   // HBA Reset
#define AHCI_GHC_IE        (1U << 1)   // Interrupt Enable
#define AHCI_GHC_MRSM      (1U << 2)   // MSI Revert to Single Message
#define AHCI_GHC_AE        (1U << 31)  // AHCI Enable

// CAP bit flags
#define AHCI_CAP_NP_MASK   0x1F         // Number of Ports (bits 4:0), value is N-1
#define AHCI_CAP_NCS_SHIFT 8
#define AHCI_CAP_NCS_MASK  (0x1F << 8)  // Number of Command Slots, value is N-1
#define AHCI_CAP_SSS       (1U << 27)   // Staggered Spin-up Supported
#define AHCI_CAP_SAM       (1U << 18)   // Supports AHCI Mode Only
#define AHCI_CAP_S64A      (1U << 31)   // 64-bit Addressing Supported

// ------------------------------------------------------------------------------------------------
// AHCI Port Register Offsets  (relative to port base = ABAR + 0x100 + port * 0x80)
// ------------------------------------------------------------------------------------------------
#define AHCI_PORT_BASE(port)    (0x100 + (port) * 0x80)

#define AHCI_PXCLB    0x00  // Command List Base Address (low)
#define AHCI_PXCLBU   0x04  // Command List Base Address (high)
#define AHCI_PXFB     0x08  // FIS Base Address (low)
#define AHCI_PXFBU    0x0C  // FIS Base Address (high)
#define AHCI_PXIS     0x10  // Interrupt Status
#define AHCI_PXIE     0x14  // Interrupt Enable
#define AHCI_PXCMD    0x18  // Command and Status
#define AHCI_PXTFD    0x20  // Task File Data
#define AHCI_PXSIG    0x24  // Signature
#define AHCI_PXSSTS   0x28  // Serial ATA Status (SCR0: SStatus)
#define AHCI_PXSCTL   0x2C  // Serial ATA Control (SCR2: SControl)
#define AHCI_PXSERR   0x30  // Serial ATA Error   (SCR1: SError)
#define AHCI_PXSACT   0x34  // Serial ATA Active  (SCR3: SActive)
#define AHCI_PXCI     0x38  // Command Issue
#define AHCI_PXSNTF   0x3C  // Serial ATA Notification

// PxCMD bits
#define AHCI_CMD_ST    (1U << 0)   // Start (DMA engine)
#define AHCI_CMD_SUD   (1U << 1)   // Spin-Up Device
#define AHCI_CMD_POD   (1U << 2)   // Power On Device
#define AHCI_CMD_FRE   (1U << 4)   // FIS Receive Enable
#define AHCI_CMD_FR    (1U << 14)  // FIS Receive Running
#define AHCI_CMD_CR    (1U << 15)  // Command List Running

// PxTFD bits
#define AHCI_TFD_ERR   (1U << 0)
#define AHCI_TFD_DRQ   (1U << 3)
#define AHCI_TFD_BSY   (1U << 7)

// PxSSTS DET field (bits 3:0)
#define AHCI_SSTS_DET_MASK    0x0F
#define AHCI_SSTS_DET_PRESENT 0x03  // Device present and PHY comm established
#define AHCI_SSTS_DET_OFFLINE 0x04  // PHY in offline mode

// PxSIG — device type signatures
#define AHCI_SIG_SATA  0x00000101  // SATA drive
#define AHCI_SIG_ATAPI 0xEB140101  // SATAPI (ATAPI) device
#define AHCI_SIG_SEMB  0xC33C0101  // Enclosure Management Bridge
#define AHCI_SIG_PM    0x96690101  // Port Multiplier

// ------------------------------------------------------------------------------------------------
// AHCI Command List / FIS Structures
// ------------------------------------------------------------------------------------------------

// Command Header (32 bytes, 32 entries per port)
	typedef struct {
		uint16_t flags;     // [4:0]=CFL, [5]=A(ATAPI), [6]=W(write), [7]=P(prefetch), [8]=R(reset), [9]=B(BIST), [10]=C(clear busy)
		uint16_t prdtl;     // Physical Region Descriptor Table Length (entry count)
		uint32_t prdbc;     // PRD Byte Count (bytes transferred, written by HBA)
		uint32_t ctba;      // Command Table Base Address (low, 128-byte aligned)
		uint32_t ctbau;     // Command Table Base Address (high)
		uint32_t reserved[4];
	} __attribute__((packed)) ahci_cmd_header_t;

#define AHCI_CMD_HDR_FLAG_CFL_MASK  0x001F  // Command FIS Length in dwords
#define AHCI_CMD_HDR_FLAG_ATAPI     (1U << 5)
#define AHCI_CMD_HDR_FLAG_WRITE     (1U << 6)
#define AHCI_CMD_HDR_FLAG_PREFETCH  (1U << 7)
#define AHCI_CMD_HDR_FLAG_RESET     (1U << 8)
#define AHCI_CMD_HDR_FLAG_CLR_BUSY  (1U << 10)

// Physical Region Descriptor (8 bytes per entry)
	typedef struct {
		uint32_t dba;       // Data Base Address (low)
		uint32_t dbau;      // Data Base Address (high)
		uint32_t reserved;
		uint32_t dbc;       // Byte count [21:0], bit 31 = interrupt on completion; count is (N-1)
	} __attribute__((packed)) ahci_prd_t;

#define AHCI_PRD_MAX_BYTES  (4 * 1024 * 1024)  // 4 MiB per PRD entry
#define AHCI_PRD_IOC        (1U << 31)           // Interrupt on completion

// Register Host to Device FIS (H2D FIS), type 0x27
	typedef struct {
		uint8_t  fis_type;   // 0x27
		uint8_t  pmport_c;   // [3:0]=port multiplier, [7]=C (1=command, 0=control)
		uint8_t  command;    // ATA command
		uint8_t  featurel;   // Feature (low)
		uint8_t  lba0;       // LBA [7:0]
		uint8_t  lba1;       // LBA [15:8]
		uint8_t  lba2;       // LBA [23:16]
		uint8_t  device;     // Device register
		uint8_t  lba3;       // LBA [31:24]
		uint8_t  lba4;       // LBA [39:32]
		uint8_t  lba5;       // LBA [47:40]
		uint8_t  featureh;   // Feature (high)
		uint16_t count;      // Sector count
		uint8_t  icc;        // Isochronous command completion
		uint8_t  control;    // Control register
		uint8_t  reserved[4];
	} __attribute__((packed)) ahci_fis_h2d_t;

#define AHCI_FIS_TYPE_H2D   0x27
#define AHCI_FIS_H2D_C_BIT  (1U << 7)  // Command bit in pmport_c

// Command Table (variable size, at least 128 bytes + PRDT)
// Layout: [0x00] CFIS (64 bytes) | [0x40] ACMD (16 bytes) | [0x50] reserved (48 bytes) | [0x80] PRDT
#define AHCI_CMDT_CFIS_OFF  0x00
#define AHCI_CMDT_ACMD_OFF  0x40
#define AHCI_CMDT_PRDT_OFF  0x80
#define AHCI_CMDT_PRDT_SIZE(n_prd) (sizeof(ahci_prd_t) * (n_prd))
#define AHCI_CMDT_TOTAL_SIZE(n_prd) (0x80 + AHCI_CMDT_PRDT_SIZE(n_prd))

// ------------------------------------------------------------------------------------------------
// ATA Commands used by this driver
// ------------------------------------------------------------------------------------------------
#define ATA_CMD_IDENTIFY         0xEC
#define ATA_CMD_READ_DMA_EXT     0x25
#define ATA_CMD_WRITE_DMA_EXT    0x35
#define ATA_CMD_FLUSH_CACHE_EXT  0xEA

#define ATA_DEV_LBA  (1U << 6)  // Device register: LBA mode

// ------------------------------------------------------------------------------------------------
// AHCI Port State
// ------------------------------------------------------------------------------------------------

// How many command slots we actually use (cap is up to 32, we cap at 32)
#define AHCI_MAX_SLOTS  32
// Max PRDs per command table
#define AHCI_MAX_PRD    8

// Per-port memory region (physically contiguous, 4K-aligned recommended)
	typedef struct {
		ahci_cmd_header_t   cmd_list[32];               // Command List (1KB)
		uint8_t             fis_buf[256];               // Received FIS buffer (256 bytes)
		// One command table per slot (we keep one pre-built)
		uint8_t             cmd_table[AHCI_CMDT_TOTAL_SIZE(AHCI_MAX_PRD)];
	} __attribute__((aligned(4096))) ahci_port_mem_t;

	typedef enum {
		AHCI_DEV_NONE = 0,
		AHCI_DEV_SATA = 1,
		AHCI_DEV_SATAPI = 2,
		AHCI_DEV_SEMB = 3,
		AHCI_DEV_PM = 4,
	} ahci_dev_type_t;

	typedef struct {
		int             present;         // Is a device connected
		ahci_dev_type_t type;
		uint32_t        port_idx;        // Which port number (0-31)
		uintptr_t       port_base;       // MMIO address of port registers
		ahci_port_mem_t* mem;            // DMA-accessible port memory
		uint64_t        sector_count;
		uint16_t        sector_size;
		char            model[41];
		char            serial[21];
	} ahci_port_t;


	// Per-controller private state (stored in wallos_device_t::driver_data)
	typedef struct {
		uintptr_t   abar;               // AHCI Base Address (MMIO)
		uintptr_t   abar_phys;          // Physical address (for reference / unmap)
		uint32_t    port_count;         // Ports actually implemented
		uint32_t    slot_count;         // Command slots per port
		uint8_t     supports_64bit;
		ahci_port_t ports[32];
	} ahci_ctrl_t;

	// Driver registration
	void ahci_register_driver(void);

	// Driver ops
	// These probably shouldn't be called directly
	int  ahci_probe(wallos_device_t* dev);
	void ahci_attach(wallos_device_t* dev);
	void ahci_detach(wallos_device_t* dev);

	// I/O (synchronous, polling)
	int ahci_read_sectors(ahci_port_t* port, uint64_t lba, uint32_t count, void* buf);
	int ahci_write_sectors(ahci_port_t* port, uint64_t lba, uint32_t count, const void* buf);

#ifdef __cplusplus
}
#endif

#endif // WALLOS_AHCI_H