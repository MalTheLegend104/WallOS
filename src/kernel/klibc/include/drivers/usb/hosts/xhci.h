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


	typedef struct {
		uintptr_t mmio_base;
		size_t mmio_size;

		volatile xhci_cap_regs_t* cap;
		volatile xhci_op_regs_t* op;
		volatile xhci_runtime_regs_t* runtime;
		volatile xhci_doorbell_regs_t* doorbell;

		bool ac64;
		bool csz;

	} xhci_controller_t;

	void xhci_init();

#ifdef __cplusplus	
#undef _Static_assert
}
#endif
#endif // WALLOS_XHCI_H