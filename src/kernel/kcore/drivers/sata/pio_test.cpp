#include <drivers/sata/pio.h>
#include <drivers/serial.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Assuming printf_serial is declared elsewhere and works like printf but targets serial.
// Example: int printf_serial(const char *format, ...);

void dump_hex(const uint8_t* data, int count) {
    // Modified: Use printf_serial for hex dumps
    for (int i = 0; i < count; i++) {
        if (i % 16 == 0) printf_serial("\r\n%04x: ", i);
        printf_serial("%02x ", data[i]);
    }
    printf_serial("\r\n");
}

void test_read_sector(int drive, uint32_t lba) {
    uint8_t buffer[512];

    printf("\n[TEST] Reading LBA %u on drive %d\n", lba, drive);
    printf_serial("\r\n[TEST] Reading LBA %u on drive %d\r\n", lba, drive);

    if (!sata_pio_read28(drive, lba, 1, buffer)) {
        printf("[ERROR] Read failed.\n");
        printf_serial("[ERROR] Read failed.\r\n");
        return;
    }

    printf("[OK] Read success. Dumping first 64 bytes to serial.");
    printf_serial("\r\n[OK] Read success. Dumping first 64 bytes:\r\n");
    dump_hex(buffer, 64);
}

void test_write_sector(int drive, uint32_t lba) {
    uint8_t writebuf[512];
    uint8_t readbuf[512];

    // Fill with a test pattern
    for (int i = 0; i < 512; i++) writebuf[i] = (uint8_t) (i + 7);

    printf("\n[TEST] Writing pattern to LBA %u on drive %d\n", lba, drive);
    printf_serial("\n[TEST] Writing pattern to LBA %u on drive %d\r\n", lba, drive);

    if (!sata_pio_write28(drive, lba, 1, writebuf)) {
        printf("[ERROR] Write failed.\n");
        printf_serial("[ERROR] Write failed.\r\n");
        return;
    }

    // Read back
    if (!sata_pio_read28(drive, lba, 1, readbuf)) {
        printf("[ERROR] Verification read failed.\n");
        printf_serial("[ERROR] Verification read failed.\r\n");
        return;
    }

    if (memcmp(writebuf, readbuf, 512) == 0) {
        printf("[OK] Write verification passed.\n");
        printf_serial("[OK] Write verification passed.\r\n");
    } else {
        printf("[ERROR] Write verification FAILED. Dumping buffers to serial:\n");
        printf_serial("[ERROR] Write verification FAILED. Dumping buffers:\r\n");

        printf_serial("[Expected]");
        dump_hex(writebuf, 64);

        printf_serial("[Got]");
        dump_hex(readbuf, 64);
    }
}

void test_mbr_dump(int drive) {
    uint8_t sector[512];

    printf("\n[TEST] Dumping MBR from drive %d\n", drive);
    printf_serial("\n[TEST] Dumping MBR from drive %d\r\n", drive);

    if (!sata_pio_read28(drive, 0, 1, sector)) {
        printf("[ERROR] MBR read failed.\n");
        printf_serial("[ERROR] MBR read failed.\r\n");
        return;
    }

    // Modified: Use printf for the header, but dump_hex uses printf_serial
    printf("[OK] MBR first 64 bytes (serial dump):");
    printf_serial("[OK] MBR first 64 bytes:\r\n");
    dump_hex(sector, 64);

    uint16_t sig = *(uint16_t*) &sector[510];
    if (sig == 0xAA55) {
        printf("[OK] Valid MBR signature 0xAA55\n");
        printf_serial("[OK] Valid MBR signature 0xAA55\r\n");
    } else {
        printf("[WARN] Invalid MBR signature: %04x\n", sig);
        printf_serial("[WARN] Invalid MBR signature: %04x\r\n", sig);
    }

// Additionally, the partition table entries are longer output, so use serial
    printf_serial("\r\nMBR Partition Table Entries (Bytes 446-509):\r\n");
    printf_serial("Offset | Status | Head | Sector | Cyl | Type | Head | Sector | Cyl | LBA Start | Sector\r\n");
    printf_serial("-------|--------|------|--------|-----|------|------|--------|-----|-----------|--------\r\n");

    for (int i = 0; i < 4; i++) {
        uint8_t* p = &sector[0x1BE + i * 16]; // Partition entry offset (446 + i*16)

        // This is complex, longer output so it goes to serial
        printf_serial("0x%03x |  %02x    | %02x   |  %02x    | %02x  | %02x   | %02x   |  %02x    | %02x  | %08x  | %08x\r\n",
            0x1BE + i * 16, // Offset
            p[0], // Status
            p[1], p[2] & 0x3F, p[3], // CHS Start
            p[4], // Type
            p[5], p[6] & 0x3F, p[7], // CHS End
            *(uint32_t*) &p[8],  // LBA Start
            *(uint32_t*) &p[12] // Sectors
        );
    }
}

void test_lba_walk(int drive) {
    printf("\n[TEST] Walking read over first 16 LBAs of drive %d\n", drive);
    printf_serial("\n[TEST] Walking read over first 16 LBAs of drive %d\r\n", drive);

    uint8_t buf[512];
    for (int lba = 0; lba < 16; lba++) {
        // These are short, non-dump outputs, so they stay with printf
        if (sata_pio_read28(drive, lba, 1, buf)) {
            printf("  LBA %-3d OK, first byte = %02x\n", lba, buf[0]);
            printf_serial("  LBA %-3d OK, first byte = %02x\r\n", lba, buf[0]);
        } else {
            printf("  LBA %-3d READ FAILED\n", lba);
            printf_serial("  LBA %-3d READ FAILED\r\n", lba);
        }
    }
}