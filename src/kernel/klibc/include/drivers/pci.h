#ifndef WALLOS_PCI_H
#define WALLOS_PCI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	// void pci_init_discovery();
	// void pci_scan_bus(uint16_t seg, uint8_t bus);
	// uint32_t pci_config_read32(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset);

	void pci_discover();



	typedef struct {
		uint8_t base_class;
		uint8_t sub_class; /* 0xFF indicates the base class name itself */
		const char* short_name;
	} pci_class_short_t;


	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// This was hand made from the pci_classes[] array in pci_dev.h
	// If more classes are added in the future, this must be manually updated.
	// 
	// Guide to adding things:
	// 
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	static const pci_class_short_t pci_class_short[] = {
		/* 0x00 - Unclassified */
		{0x00, 0xff, "unclassified"},
		{0x00, 0x00, "unclassified_nonvga"},
		{0x00, 0x01, "unclassified_vga"},
		{0x00, 0x05, "image_coprocessor"},

		/* 0x01 - Mass Storage */
		{0x01, 0xff, "msc"},          // mass storage controller
		{0x01, 0x00, "scsi"},         // SCSI
		{0x01, 0x01, "ide"},          // IDE
		{0x01, 0x02, "floppy"},       // Floppy
		{0x01, 0x03, "ipi"},          // IPI bus
		{0x01, 0x04, "raid"},         // RAID
		{0x01, 0x05, "ata"},          // ATA (PATA)
		{0x01, 0x06, "sata"},         // SATA
		{0x01, 0x07, "sas"},          // Serial Attached SCSI
		{0x01, 0x08, "nvme"},         // NVMe
		{0x01, 0x09, "ufs"},          // Universal Flash Storage
		{0x01, 0x80, "storage_cont"}, // Generic

		/* 0x02 - Network */
		// I don't really know the difference in these.
		// I've only ever seen ethernet come up in testing.
		{0x02, 0xff, "net_cont"},  // Generic
		{0x02, 0x00, "eth"},       // Ethernet
		{0x02, 0x01, "tokenring"},
		{0x02, 0x02, "fddi"},      // FDDI 
		{0x02, 0x03, "atm"},       // ATM 
		{0x02, 0x04, "isdn"},
		{0x02, 0x05, "worldfip"},
		{0x02, 0x06, "picmg"},
		{0x02, 0x07, "infiniband"},
		{0x02, 0x08, "fabric"},
		{0x02, 0x80, "net_cont"},  // Generic

		/* 0x03 - Display */
		{0x03, 0xff, "display_cont"}, // Generic
		{0x03, 0x00, "vga"},          // VGA Controller
		{0x03, 0x01, "xga"},          // XGA Controller
		{0x03, 0x02, "3d"},           // 3d Controller
		{0x03, 0x80, "display_cont"}, // Generic

		/* 0x04 - Multimedia */
		// These are descriptive enough as is
		{0x04, 0xff, "multimedia_cont"},      // Generic
		{0x04, 0x00, "multimedia_video"},
		{0x04, 0x01, "multimedia_audio"},
		{0x04, 0x02, "multimedia_telephony"},
		{0x04, 0x80, "multimedia_cont"},

		// Generic audio device, part of multimedia
		{0x04, 0x03, "audio_dev"},

		/* 0x05 - Memory */
		// I've never seen these come up.
		// They're self descriptive enough
		{0x05, 0xff, "mem_cont"},
		{0x05, 0x00, "ram"},
		{0x05, 0x01, "flash"},
		{0x05, 0x02, "cxl"},
		{0x05, 0x80, "mem_cont"},

		/* 0x06 - Bridge */
		// Most of these are super descriptive, I've never seen most in practice.
		// ISA and PCI are common, and I want their shorthand to be very short.
		{0x06, 0xff, "bridge"},
		{0x06, 0x00, "bridge_host"},
		{0x06, 0x01, "isa"},
		{0x06, 0x02, "bridge_eisa"},
		{0x06, 0x03, "bridge_mca"},
		{0x06, 0x04, "pci"},
		{0x06, 0x05, "bridge_pcmcia"},
		{0x06, 0x06, "bridge_nubus"},
		{0x06, 0x07, "bridge_cardbus"},
		{0x06, 0x08, "bridge_raceway"},
		{0x06, 0x09, "bridge_pci_semitransparent"},
		{0x06, 0x0a, "bridge_infiniband_host"},
		{0x06, 0x80, "bridge"},

		/* 0x07 - Communication */
		// Serial and Parallel are the only ones we care about from these
		// The others are long so it's obvious what they are if they come up.
		{0x07, 0xff, "comm_cont"},
		{0x07, 0x00, "serial"},
		{0x07, 0x01, "parallel"},
		{0x07, 0x02, "comm_multi_serial"},
		{0x07, 0x03, "comm_modem"},
		{0x07, 0x04, "comm_gpib"},
		{0x07, 0x05, "comm_smartcard"},
		{0x07, 0x80, "comm_cont"},

		/* 0x08 - System Peripheral */
		{0x08, 0xff, "sys_cont"},
		{0x08, 0x00, "pic"},        // Programmable Interrupt Controller
		{0x08, 0x01, "dma"},        // DMA Controller
		{0x08, 0x02, "timer"},      // System Timer
		{0x08, 0x03, "rtc"},        // Real Time Clock
		{0x08, 0x04, "hotplug"},    // PCI Hot-Plug Controller
		{0x08, 0x05, "sd_host"},    // SD Host Controller
		{0x08, 0x06, "iommu"},      // IOMMU Controller
		{0x08, 0x07, "rcec"},       // Root Complex Event Collector
		{0x08, 0x80, "sys_cont"},
		{0x08, 0x99, "sys_timing"}, // Generic Timing Card

		/* 0x09 - Input */
		{0x09, 0xff, "input_cont"},
		{0x09, 0x00, "kbd"},           // Keyboard
		{0x09, 0x01, "pen"},           // Pen
		{0x09, 0x02, "mouse"},         // Mouse
		{0x09, 0x03, "input_scanner"},
		{0x09, 0x04, "gameport"},      // Gameport (I hope to find an x86_64 PC with a gameport one day)
		{0x09, 0x80, "input_cont"},

		/* 0x0A - Docking */
		{0x0a, 0xff, "dock"},
		{0x0a, 0x00, "dock_generic"},
		{0x0a, 0x80, "dock"},

		/* 0x0B - Processor */
		// I have *zero* clue how any of these would ever show up.
		// At least not on x86_64
		{0x0b, 0xff, "cpu"},
		{0x0b, 0x00, "cpu_386"},
		{0x0b, 0x01, "cpu_486"},
		{0x0b, 0x02, "cpu_pentium"},
		{0x0b, 0x10, "cpu_alpha"},
		{0x0b, 0x20, "cpu_ppc"},
		{0x0b, 0x30, "cpu_mips"},
		{0x0b, 0x40, "cpu_coproc"},

		/* 0x0C - Serial Bus */
		{0x0c, 0xff, "serial_bus"},
		{0x0c, 0x00, "firewire"},
		{0x0c, 0x01, "access_bus"},
		{0x0c, 0x02, "ssa_cont"},
		{0x0c, 0x03, "usb"},
		{0x0c, 0x04, "fibre_channel"},
		{0x0c, 0x05, "smbus"},
		{0x0c, 0x06, "infiniband"},
		{0x0c, 0x07, "ipmi"},
		{0x0c, 0x08, "sercos"},
		{0x0c, 0x09, "canbus"},
		{0x0c, 0x0a, "i3c"}, // MIPI I3C
		{0x0c, 0x80, "serial_bus"},

		/* 0x0D - Wireless */
		{0x0d, 0xff, "wireless_cont"},
		{0x0d, 0x00, "irda"},
		{0x0d, 0x01, "ir"},
		{0x0d, 0x10, "rf"},
		{0x0d, 0x11, "bluetooth"},
		{0x0d, 0x12, "broadband"},
		{0x0d, 0x20, "wifi5g"},
		{0x0d, 0x21, "wifi2g"},
		{0x0d, 0x40, "cellular"},
		{0x0d, 0x41, "eth_cellular"},
		{0x0d, 0x80, "wireless_cont"},

		/* 0x0E - Intelligent */
		{0x0e, 0xff, "intelligent_cont"},
		{0x0e, 0x00, "i2o"},

		/* 0x0F - Satellite */
		{0x0f, 0xff, "satellite_cont"},
		{0x0f, 0x01, "sat_tv"},
		{0x0f, 0x02, "sat_audio"},
		{0x0f, 0x03, "sat_voice"},
		{0x0f, 0x04, "sat_data"},

		/* 0x10 - Encryption */
		{0x10, 0xff, "crypto_cont"},
		{0x10, 0x00, "crypto_net"},
		{0x10, 0x10, "crypto_entertainment"},
		{0x10, 0x80, "crypto_cont"},

		/* 0x11 - Signal Processing */
		{0x11, 0xff, "signal_proc"},
		{0x11, 0x00, "signal_dpio"},
		{0x11, 0x01, "signal_perf"},
		{0x11, 0x10, "signal_sync"},
		{0x11, 0x20, "signal_mgmt"},
		{0x11, 0x80, "signal_proc"},

		/* 0x12 - Accelerators */
		{0x12, 0xff, "accelerator"},
		{0x12, 0x00, "accelerator"},
		{0x12, 0x01, "accelerator_sdxi"},

		/* 0x13 - Instrumentation */
		{0x13, 0xff, "instrumentation"},

		/* 0x40 - Coprocessor */
		{0x40, 0xff, "coprocessor"},

		/* 0xFF - Unknown */
		{0xff, 0xff, "unknown"},
	};

	static const unsigned int pci_class_short_count = sizeof(pci_class_short) / sizeof(pci_class_short[0]);

	static inline const char* get_pci_class_name_short(uint8_t base_id, uint8_t sub_id) {
		const char* fallback = "unkown";
		for (unsigned int i = 0; i < pci_class_short_count; i++) {
			if (pci_class_short[i].base_class == base_id) {
				if (pci_class_short[i].sub_class == sub_id) return pci_class_short[i].short_name;
				if (pci_class_short[i].sub_class == 0xff) fallback = pci_class_short[i].short_name;
			}
		}
		return fallback;
	}

#ifdef __cplusplus
}
#endif

#endif // WALLOS_PCI_H