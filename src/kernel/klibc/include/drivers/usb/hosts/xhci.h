#ifndef WALLOS_XHCI_H
#define WALLOS_XHCI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
#ifndef _Static_assert
#define _Static_assert static_assert
#endif
extern "C" {
#endif

	typedef struct {
		uint8_t caplength;       // 0x00
		uint8_t reserved;
		uint16_t hciversion;     // 0x02
		uint32_t hcsparams1;     // 0x04
		uint32_t hcsparams2;     // 0x08
		uint32_t hcsparams3;     // 0x0C
		uint32_t hccparams1;     // 0x10
		uint32_t dboff;          // 0x14
		uint32_t rtsoff;         // 0x18
		uint32_t hccparams2;     // 0x1C
		// there is technically a VTIO register after HCCPARAMS2, but we wont use it and it's not necessarily present on all controllers.
	} __attribute__((packed)) xhci_cap_regs_t;
	_Static_assert(offsetof(xhci_cap_regs_t, caplength) == 0x00, "caplength offset");
	_Static_assert(offsetof(xhci_cap_regs_t, hciversion) == 0x02, "hciversion offset");
	_Static_assert(offsetof(xhci_cap_regs_t, hcsparams1) == 0x04, "hcsparams1 offset");
	_Static_assert(offsetof(xhci_cap_regs_t, hcsparams2) == 0x08, "hcsparams2 offset");
	_Static_assert(offsetof(xhci_cap_regs_t, hcsparams3) == 0x0C, "hcsparams3 offset");
	_Static_assert(offsetof(xhci_cap_regs_t, hccparams1) == 0x10, "hccparams1 offset");
	_Static_assert(offsetof(xhci_cap_regs_t, dboff) == 0x14, "dboff offset");
	_Static_assert(offsetof(xhci_cap_regs_t, rtsoff) == 0x18, "rtsoff offset");
	_Static_assert(offsetof(xhci_cap_regs_t, hccparams2) == 0x1C, "hccparams2 offset");
	_Static_assert(sizeof(xhci_cap_regs_t) == 0x20, "xhci_cap_regs_t size");


	typedef struct {
		uint32_t usbcmd;         // 0x00
		uint32_t usbsts;         // 0x04
		uint32_t pagesize;       // 0x08
		uint32_t reserved0[2];
		uint32_t dnctrl;         // 0x14
		uint64_t crcr;           // 0x18
		uint32_t reserved1[4];
		uint64_t dcbaap;         // 0x30
		uint32_t config;         // 0x38

		// Port registers follow here
	} __attribute__((packed)) xhci_op_regs_t;
	_Static_assert(offsetof(xhci_op_regs_t, usbcmd) == 0x00, "usbcmd offset");
	_Static_assert(offsetof(xhci_op_regs_t, usbsts) == 0x04, "usbsts offset");
	_Static_assert(offsetof(xhci_op_regs_t, pagesize) == 0x08, "pagesize offset");
	_Static_assert(offsetof(xhci_op_regs_t, dnctrl) == 0x14, "dnctrl offset");
	_Static_assert(offsetof(xhci_op_regs_t, crcr) == 0x18, "crcr offset");
	_Static_assert(offsetof(xhci_op_regs_t, dcbaap) == 0x30, "dcbaap offset");
	_Static_assert(offsetof(xhci_op_regs_t, config) == 0x38, "config offset");
	_Static_assert(offsetof(xhci_op_regs_t, crcr) % 8 == 0, "crcr must be 8-byte aligned");
	_Static_assert(offsetof(xhci_op_regs_t, dcbaap) % 8 == 0, "dcbaap must be 8-byte aligned");
	_Static_assert(sizeof(xhci_op_regs_t) == 0x3C, "xhci_op_regs_t size");


	typedef struct {
		uint32_t mfindex;        // 0x00
		uint32_t reserved0[7];

		// Interrupter registers follow
	} __attribute__((packed)) xhci_runtime_regs_t;
	_Static_assert(offsetof(xhci_runtime_regs_t, mfindex) == 0x00, "mfindex offset");
	_Static_assert(sizeof(xhci_runtime_regs_t) == 0x20, "xhci_runtime_regs_t size");


	typedef struct {
		uint32_t db[256];
	} __attribute__((packed)) xhci_doorbell_regs_t;
	_Static_assert(offsetof(xhci_doorbell_regs_t, db) == 0x00, "db offset");
	_Static_assert(sizeof(xhci_doorbell_regs_t) == 0x400, "xhci_doorbell_regs_t size");

	typedef struct {
		uint16_t xhci_extended_cap_ptr;
		uint8_t max_psa_size;
		bool cfc;  // Contiguous Frame ID
		bool sec;  // Stopped EDTLA
		bool spc;  // Stopped Short Packet
		bool pae;  //  Parse All Event
		bool nss;  // No Secondary SID Support
		bool ltc;  // Latency Tolerance Messaging
		bool lhrc; // Light HC Reset
		bool pind; // Port Indicators
		bool ppc;  // Port Power Control
		bool csz;  // Context Size
		bool bnc;  // BW Negotiation
		bool ac64; // 64 Addressing
	} hccparams1_t;

	typedef struct {
		bool vtc; // Virtualization Based Trusted I/O 
		bool gsc; // Get/Set Extended Property 
		bool etc_tsc; // Extended TBC TRB Status 
		bool etc; // Extended TBC 
		bool cic; // Configuration Information 
		bool lec; // Large ESIT Payload 
		bool ctc; // Compliance Transition 
		bool fsc; // Force Save Context 
		bool cmc; // Configure Endpoint Command Max Exit Latency Too Large 
		bool u3c; // U3 Entry 
	} hccparams2_t;

	typedef enum {
		XEC_RESERVED = 0,
		XEC_USB_LEGACY = 1,
		XEC_SUPPORTED_PROTO = 2,
		XEC_EXT_POWER_MANAGEMENT = 3,
		XEC_IO_VIRT = 4,
		XEC_MESSAGE_INTERRUPT = 5,
		XEC_LOCAL_MEMORY = 6,
		XEC_USB_DEBUG = 10,
		XEC_EXT_MESSAGE_INTERRUPT = 17,
		XEC_VENDOR_DEFINED = 255
		// There are reserved slots from 7-9, 11-16, 18-191
	} xhci_xec_capability_id_t;

	typedef struct {
		/* DWORD 0: USBLEGSUP */
		uint8_t  cap_id;                        // Bits 7:0   - Capability ID (Expected: 1)
		uint8_t  next_cap_ptr;                  // Bits 15:8  - Next Capability Pointer
		bool     hc_bios_owned;                 // Bit  16    - HC BIOS Owned Semaphore
		bool     hc_os_owned;                   // Bit  24    - HC OS Owned Semaphore

		/* DWORD 1: USBLEGCTLSTS (USB Legacy Support Control/Status) */
		/* Enables */
		bool     usb_smi_enable;                // Bit  0     - USB SMI Enable
		bool     smi_on_host_sys_err_enable;    // Bit  1     - SMI on Host System Error Enable
		bool     smi_on_os_ownership_enable;    // Bit  2     - SMI on OS Ownership Enable
		bool     smi_on_pci_command_enable;     // Bit  3     - SMI on PCI Command Enable
		bool     smi_on_bar_enable;             // Bit  4     - SMI on BAR Enable
		bool     smi_on_event_int_enable;       // Bit  13    - SMI on Event Interrupt Enable

		/* Statuses */
		bool     smi_on_host_sys_err;           // Bit  16    - SMI on Host System Error
		bool     smi_on_os_ownership_change;    // Bit  17    - SMI on OS Ownership Change
		bool     smi_on_pci_command;            // Bit  18    - SMI on PCI Command
		bool     smi_on_bar;                    // Bit  19    - SMI on BAR
		bool     smi_on_event_int;              // Bit  29    - SMI on Event Interrupt
	} xhci_xec_legacy_support_t;

	typedef struct {
		uint8_t  psiv;                     // Bits 3:0   - Protocol Speed ID Value
		uint8_t  psie;                     // Bits 5:4   - Protocol Speed ID Exponent (0=Bps, 1=Kbps, 2=Mbps, 3=Gbps)
		bool     pfd;                      // Bit  6     - PSI Full Duplex
		// bits 9-13 are RsvdP
		uint8_t  lp;                       // Bits 15:14 - Link Protocol (0=Sys, 1=Gen1, 2=Gen2)
		uint16_t proto_speed_id_mantissa;  // Bits 31:16 - Speed Mantissa
	} xhci_xec_supported_proto_psi_t;

	typedef struct {
		/* First DWORD */
		// Revision is stored as BCD, this is the converted values
		uint8_t rev_major; // Bits 31:24 (BCD)
		uint8_t rev_minor; // Bits 23:16 (BCD)

		/* Second DWORD */
		char name_string[5]; // spec has 4 characters, 5 allows for '\0'

		/* Third DWORD */
		uint8_t comp_port_offset; // Bits 7:0   - Compatible Port Offset
		uint8_t comp_port_count;  // Bits 15:8  - Compatible Port Count
		uint16_t proto_defined;   // Bits 27:16 - Protocol Defined
		uint8_t psic;             // Bits 31:28 - Protocol Speed ID Count

		/* Fourth DWORD */
		uint8_t proto_slot_type;           // Bits 4:0   - Protocol Slot Type (0 = default, 1 = IC-USB, etc.)
		// rest of DWORD is RsvdP

		/* Dynamic PSI Entries (Allocated starting at Fifth DWORD if psic > 0) */
		xhci_xec_supported_proto_psi_t* psi_array; // TODO: Ensure this is freed
		uint8_t psi_count;
	} xhci_xec_supported_proto_t;

	typedef struct xhci_extended_compat {
		xhci_xec_capability_id_t capability;
		void* addr;
		void* next_xec_addr;

		// TODO: This needs to be freed on destruction BEFORE we free the surrounding struct
		// In theory, we will never need to detach USB host controllers (unless something is really wrong), but still want detach to do it's best effort
		void* specific_data;

		struct xhci_extended_compat* next_node;
	} xhci_extended_compat_t;


	typedef struct {
		uintptr_t mmio_base;
		size_t mmio_size;

		volatile xhci_cap_regs_t* cap;
		volatile xhci_op_regs_t* op;
		volatile xhci_runtime_regs_t* runtime;
		volatile xhci_doorbell_regs_t* doorbell;

		xhci_extended_compat_t* first_xce;
		xhci_extended_compat_t* last_xce;

		uintptr_t dcbaa_phys;
		uint64_t* dcbaa;
		uint8_t dcbaa_size;

		uint8_t max_slots;

		bool ac64;
		bool csz;

	} xhci_controller_t;

	void xhci_init();

#ifdef __cplusplus	
#undef _Static_assert
}
#endif
#endif // WALLOS_XHCI_H