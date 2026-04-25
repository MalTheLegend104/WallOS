#ifndef WALLOS_ATAPI_PIO_H
#define WALLOS_ATAPI_PIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// ATAPI sector size for CD-ROM
#define ATAPI_SECTOR_SIZE 2048

// ATAPI CDB command opcodes
#define ATAPI_CMD_INQUIRY        0x12
#define ATAPI_CMD_READ_CAPACITY  0x25
#define ATBC_CMD_READ12          0xA8

// ATAPI packet command
#define COMMAND_PACKET           0xA0

// Inquiry response structure (simplified, per SPC-4)
	typedef struct atapi_inquiry {
		uint8_t peripheral_device_type : 5;
		uint8_t peripheral_qualifier : 3;
		uint8_t reserved0 : 7;
		uint8_t removable : 1;
		uint8_t version;
		uint8_t response_data_format : 4;
		uint8_t hi_support : 1;
		uint8_t norm_aca : 1;
		uint8_t obsolete0 : 2;
		uint8_t additional_length;
		uint8_t protect : 1;
		uint8_t reserved1 : 2;
		uint8_t third_party_copy : 1;
		uint8_t target_port_group : 2;
		uint8_t access_controls : 1;
		uint8_t scc : 1;
		uint8_t addr16 : 1;
		uint8_t obsolete1 : 2;
		uint8_t multi_p : 1;
		uint8_t med_chng : 1;
		uint8_t reserved2 : 2;
		uint8_t enc_serv : 1;
		uint8_t vs0 : 1;
		uint8_t cmd_que : 1;
		uint8_t reserved3 : 1;
		uint8_t obsolete2 : 2;
		uint8_t reserved4 : 1;
		uint8_t sync : 1;
		uint8_t wbus16 : 1;
		uint8_t obsolete3 : 2;
		char    vendor_id[8];
		char    product_id[16];
		char    product_rev[4];
	} __attribute__((packed)) atapi_inquiry_t;

	bool atapi_send_packet(int drive_number, const uint8_t* cdb, void* buf, uint16_t buf_len);
	bool atapi_read_capacity(int drive_number, uint32_t* lba_out, uint32_t* block_size_out);
	bool atapi_read_sectors(int drive_number, uint32_t lba, uint32_t count, void* buffer);
	bool atapi_inquiry(int drive_number, atapi_inquiry_t* out);
	void print_atapi_device_info(int drive_number);

#ifdef __cplusplus
}
#endif
#endif //WALLOS_ATAPI_PIO_H