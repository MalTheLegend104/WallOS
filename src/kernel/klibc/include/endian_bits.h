#ifndef ENDIAN_BITS_H
#define ENDIAN_BITS_H

#include <stdint.h>
#include <stddef.h>

// ------------------------------------------------------------------------------------------------
// Read/Write (8/16/24/32/64)
// ------------------------------------------------------------------------------------------------

/* 8-bit */

static inline uint8_t read8(const void* addr) { return *(const uint8_t*) addr; }
static inline void write8(void* addr, uint8_t val) { *(uint8_t*) addr = val; }

/* 16-bit */

static inline uint16_t read16le(const void* addr) { const uint8_t* p = (const uint8_t*) addr; return (uint16_t) (p[0] | (p[1] << 8)); }
static inline uint16_t read16be(const void* addr) { const uint8_t* p = (const uint8_t*) addr; return (uint16_t) ((p[0] << 8) | p[1]); }

static inline void write16le(void* addr, uint16_t val) { uint8_t* p = (uint8_t*) addr; p[0] = (uint8_t) (val); p[1] = (uint8_t) (val >> 8); }
static inline void write16be(void* addr, uint16_t val) { uint8_t* p = (uint8_t*) addr; p[0] = (uint8_t) (val >> 8); p[1] = (uint8_t) (val); }

/* 24-bit (stored as uint32_t) */

static inline uint32_t read24le(const void* addr) { const uint8_t* p = (const uint8_t*) addr; return (uint32_t) (p[0] | (p[1] << 8) | (p[2] << 16)); }
static inline uint32_t read24be(const void* addr) { const uint8_t* p = (const uint8_t*) addr; return (uint32_t) ((p[0] << 16) | (p[1] << 8) | p[2]); }

static inline void write24le(void* addr, uint32_t val) { uint8_t* p = (uint8_t*) addr; p[0] = (uint8_t) (val); p[1] = (uint8_t) (val >> 8); p[2] = (uint8_t) (val >> 16); }
static inline void write24be(void* addr, uint32_t val) { uint8_t* p = (uint8_t*) addr; p[0] = (uint8_t) (val >> 16); p[1] = (uint8_t) (val >> 8); p[2] = (uint8_t) (val); }

/* 32-bit */

static inline uint32_t read32le(const void* addr) { const uint8_t* p = (const uint8_t*) addr; return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
static inline uint32_t read32be(const void* addr) { const uint8_t* p = (const uint8_t*) addr; return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3]; }

static inline void write32le(void* addr, uint32_t val) { uint8_t* p = (uint8_t*) addr; p[0] = (uint8_t) (val); p[1] = (uint8_t) (val >> 8); p[2] = (uint8_t) (val >> 16); p[3] = (uint8_t) (val >> 24); }
static inline void write32be(void* addr, uint32_t val) { uint8_t* p = (uint8_t*) addr; p[0] = (uint8_t) (val >> 24); p[1] = (uint8_t) (val >> 16); p[2] = (uint8_t) (val >> 8); p[3] = (uint8_t) (val); }

/* 64-bit */

static inline uint64_t read64le(const void* addr) { const uint8_t* p = (const uint8_t*) addr; uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | p[i]; return v; }
static inline uint64_t read64be(const void* addr) { const uint8_t* p = (const uint8_t*) addr; uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | p[i]; return v; }

static inline void write64le(void* addr, uint64_t val) { uint8_t* p = (uint8_t*) addr; for (int i = 0; i < 8; i++) p[i] = (uint8_t) (val >> (8 * i)); }
static inline void write64be(void* addr, uint64_t val) { uint8_t* p = (uint8_t*) addr; for (int i = 0; i < 8; i++) p[i] = (uint8_t) (val >> (8 * (7 - i))); }

// ------------------------------------------------------------------------------------------------
// Bit operations
// ------------------------------------------------------------------------------------------------

/* Returns a value with bit n set. */
#define BIT(n) (1UL << (n))

/* Sets bit n in x. */
#define BIT_SET(x, n) ((x) |= BIT(n))

/* Clears bit n in x. */
#define BIT_CLEAR(x, n) ((x) &= ~BIT(n))

/* Toggles bit n in x. */
#define BIT_TOGGLE(x, n) ((x) ^= BIT(n))

/* Returns the value of bit n (0 or 1). */
#define BIT_CHECK(x, n) (((x) >> (n)) & 1UL)

/* Writes bit n in x to val (0 or non-zero). */
#define BIT_REPLACE(x, n, val) ((val) ? BIT_SET(x, n) : BIT_CLEAR(x, n))

// ------------------------------------------------------------------------------------------------
// Bit ranges
// ------------------------------------------------------------------------------------------------

/* Creates a mask with the lowest len bits set.
 *
 * Example: BITS_MASK(4) == 0xF
 */
#define BITS_MASK(len) ((1ULL << (len)) - 1ULL)

/* Extracts len bits from x starting at bit start.
 *
 * Example: BITS_GET(0xB0, 4, 4) == 0xB
 */
#define BITS_GET(x, start, len) (((x) >> (start)) & BITS_MASK(len))

/* Replaces len bits in x starting at bit start with val.
 *
 * Example: BITS_WRITE(reg, 4, 2, 2); // writes 0b10 into bits 4-5
 */
#define BITS_WRITE(x, start, len, val) ((x) = ((x) & ~(BITS_MASK(len) << (start))) | (((val) & BITS_MASK(len)) << (start)))

// ------------------------------------------------------------------------------------------------
// Masks
// ------------------------------------------------------------------------------------------------

/* Creates a mask with the lowest n bits set.
 *
 * Example: MASK(4) == 0xF
 */
#define MASK(n) BITS_MASK(n)

/* Creates a mask spanning bits hi through lo (inclusive).
 *
 * Example: GENMASK(7, 4) == 0xF0
 */
#define GENMASK(hi, lo) (MASK((hi) - (lo) + 1) << (lo))

/* Applies mask to x, preserving only masked bits. */
#define MASK_APPLY(x, mask) ((x) & (mask))

/* Inverts every bit in mask. */
#define MASK_INVERT(mask) (~(mask))

/* Sets every bit in x selected by mask. */
#define MASK_SET(x, mask) ((x) |= (mask))

/* Clears every bit in x selected by mask. */
#define MASK_CLEAR(x, mask) ((x) &= ~(mask))

/* Toggles every bit in x selected by mask. */
#define MASK_TOGGLE(x, mask) ((x) ^= (mask))

/* Replaces the bits selected by mask with the corresponding bits from val.
 * val must already be aligned to the mask.
 *
 * Example: MASK_WRITE(reg, 0x30, 0x20); // writes 0b10 to bits 4-5
 */
#define MASK_WRITE(x, mask, val) ((x) = ((x) & ~(mask)) | ((val) & (mask)))


// ------------------------------------------------------------------------------------------------
// MMIO Read/Write (8/16/32/64)
// ------------------------------------------------------------------------------------------------

/**
 * @brief Read an 8-bit value from a memory-mapped I/O register.
 *
 * @param addr Register address.
 * @return Value read from the register.
 */
static inline uint8_t mmio_read8(const volatile void* addr) {
	return *(const volatile uint8_t*) addr;
}

/**
 * @brief Read a 16-bit value from a memory-mapped I/O register.
 *
 * @param addr Register address.
 * @return Value read from the register.
 */
static inline uint16_t mmio_read16(const volatile void* addr) {
	return *(const volatile uint16_t*) addr;
}

/**
 * @brief Read a 32-bit value from a memory-mapped I/O register.
 *
 * @param addr Register address.
 * @return Value read from the register.
 */
static inline uint32_t mmio_read32(const volatile void* addr) {
	return *(const volatile uint32_t*) addr;
}

/**
 * @brief Read a 64-bit MMIO register using two 32-bit accesses.
 *
 * Reads the lower DWORD first, followed by the upper DWORD.
 * This is intended for platforms that cannot issue native 64-bit MMIO accesses.
 *
 * @param addr Register address.
 * @return 64-bit value read from the register.
 */
static inline uint64_t mmio_read64_split(const volatile void* addr) {
	const volatile uint32_t* reg = (const volatile uint32_t*) addr;

	uint32_t low = reg[0];
	uint32_t high = reg[1];

	return ((uint64_t) high << 32) | low;
}

/**
 * @brief Read a 64-bit value from a memory-mapped I/O register.
 *
 * On architectures with native 64-bit MMIO support, this performs a single 64-bit access.
 * Otherwise it falls back to two 32-bit accesses.
 *
 * @param addr Register address.
 * @return Value read from the register.
 */
static inline uint64_t mmio_read64(const volatile void* addr) {
#ifdef WALLOS_ARCH_64
	return *(const volatile uint64_t*) addr;
#else
	return mmio_read64_split(addr);
#endif
}

/**
 * @brief Write an 8-bit value to a memory-mapped I/O register.
 *
 * @param addr Register address.
 * @param value Value to write.
 */
static inline void mmio_write8(volatile void* addr, uint8_t value) {
	*(volatile uint8_t*) addr = value;
}

/**
 * @brief Write a 16-bit value to a memory-mapped I/O register.
 *
 * @param addr Register address.
 * @param value Value to write.
 */
static inline void mmio_write16(volatile void* addr, uint16_t value) {
	*(volatile uint16_t*) addr = value;
}

/**
 * @brief Write a 32-bit value to a memory-mapped I/O register.
 *
 * @param addr Register address.
 * @param value Value to write.
 */
static inline void mmio_write32(volatile void* addr, uint32_t value) {
	*(volatile uint32_t*) addr = value;
}

/**
 * @brief Write a 64-bit MMIO register using two 32-bit accesses.
 *
 * Writes the lower DWORD first, followed by the upper DWORD.
 * This was specifically written for xHCI, but should be applicable to other 64 bit mmio writes.
 *
 * @param addr Register address.
 * @param value Value to write.
 */
static inline void mmio_write64_split(volatile void* addr, uint64_t value) {
	volatile uint32_t* reg = (volatile uint32_t*) addr;

	reg[0] = (uint32_t) value;
	reg[1] = (uint32_t) (value >> 32);
}

/**
 * @brief Write a 64-bit value to a memory-mapped I/O register.
 *
 * On architectures with native 64-bit MMIO support, this performs a single 64-bit access.
 * Otherwise it falls back to two 32-bit accesses in low then high order.
 *
 * @param addr Register address.
 * @param value Value to write.
 */
static inline void mmio_write64(volatile void* addr, uint64_t value) {
#ifdef WALLOS_ARCH_64
	* (volatile uint64_t*) addr = value;
#else
	mmio_write64_split(addr, value);
#endif
}

/**
 * @brief Read a sub-dword field via a 32-bit-aligned MMIO access.
 *
 * Some MMIO devices (e.g. xHCI, per its spec section 5) only define behavior
 * for Dword/Qword accesses; 8/16-bit accesses to their register space can
 * return zero, be dropped, or otherwise misbehave (this is enforced by QEMU's
 * MemoryRegionOps access-width validation even when it wouldn't matter on
 * real hardware). This rounds addr down to the containing 4-byte boundary,
 * performs a single mmio_read32(), then extracts the requested byte-width
 * field via shift+mask.
 *
 * @param addr       Address of the field (need not be 4-byte aligned).
 * @param field_size Size of the field in bytes (1 or 2).
 * @return Extracted field value, zero-extended.
 */
static inline uint32_t mmio_readN_as32(const volatile void* addr, size_t field_size) {
	uintptr_t raw = (uintptr_t) addr;
	uintptr_t aligned = raw & ~((uintptr_t) 0x3);
	unsigned  byte_off = (unsigned) (raw - aligned);
	unsigned  shift = byte_off * 8;

	uint32_t dword = mmio_read32((const volatile void*) aligned);

	return (uint32_t) BITS_GET(dword, shift, field_size * 8);
}

static inline uint8_t mmio_read8_as32(const volatile void* addr) {
	return (uint8_t) mmio_readN_as32(addr, sizeof(uint8_t));
}

static inline uint16_t mmio_read16_as32(const volatile void* addr) {
	return (uint16_t) mmio_readN_as32(addr, sizeof(uint16_t));
}

/**
 * @brief Write a sub-dword field via a read-modify-write 32-bit-aligned MMIO access.
 *
 * WARNING:
 * Some MMIO registers (e.g. xHCI PORTSC, USBSTS) have RW1C (write-1-to-clear) bits packed into the same dword as other fields.
 * A read-modify-write on those will clear any RW1C bits currently set elsewhere in the dword, even ones you didn't intend to touch.
 * Do NOT use this on registers with RW1C semantics, instead write the full dword explicitly via mmio_write32() instead, with only the intended RW1C bits set to 1.
 *
 * @param addr       Address of the field (need not be 4-byte aligned).
 * @param field_size Size of the field in bytes (1 or 2).
 * @param value      Value to write into the field (bits beyond field_size are ignored).
 */
static inline void mmio_writeN_as32(volatile void* addr, size_t field_size, uint32_t value) {
	uintptr_t raw = (uintptr_t) addr;
	uintptr_t aligned = raw & ~((uintptr_t) 0x3);
	unsigned  byte_off = (unsigned) (raw - aligned);
	unsigned  shift = byte_off * 8;

	uint32_t mask = (uint32_t) (BITS_MASK(field_size * 8) << shift);
	uint32_t dword = mmio_read32((const volatile void*) aligned);

	MASK_WRITE(dword, mask, (value << shift));

	mmio_write32((volatile void*) aligned, dword);
}

static inline void mmio_write8_as32(volatile void* addr, uint8_t value) {
	mmio_writeN_as32(addr, sizeof(uint8_t), value);
}

static inline void mmio_write16_as32(volatile void* addr, uint16_t value) {
	mmio_writeN_as32(addr, sizeof(uint16_t), value);
}

// ------------------------------------------------------------------------------------------------
// Byte swapping / endian conversion
// ------------------------------------------------------------------------------------------------

/* Detect native CPU byte order. */
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define WALLOS_BIG_ENDIAN 1
#else
#define WALLOS_BIG_ENDIAN 0
#endif

/* Reverses the byte order of a 16-bit value. */
static inline uint16_t bswap16(uint16_t val) {
	return (uint16_t) ((val >> 8) | (val << 8));
}

/* Reverses the byte order of a 32-bit value. */
static inline uint32_t bswap32(uint32_t val) {
	return ((val >> 24) & 0x000000FFUL) |
		((val >> 8) & 0x0000FF00UL) |
		((val << 8) & 0x00FF0000UL) |
		((val << 24) & 0xFF000000UL);
}

/* Reverses the byte order of a 64-bit value. */
static inline uint64_t bswap64(uint64_t val) {
	uint64_t v = 0;
	for (int i = 0; i < 8; i++) {
		v = (v << 8) | (val & 0xFF);
		val >>= 8;
	}
	return v;
}

/* Swapping little-endian <-> big-endian is always a plain byte reversal, so these are aliases of bswap*. */
static inline uint16_t le_to_be16(uint16_t val) { return bswap16(val); }
static inline uint32_t le_to_be32(uint32_t val) { return bswap32(val); }
static inline uint64_t le_to_be64(uint64_t val) { return bswap64(val); }

static inline uint16_t be_to_le16(uint16_t val) { return bswap16(val); }
static inline uint32_t be_to_le32(uint32_t val) { return bswap32(val); }
static inline uint64_t be_to_le64(uint64_t val) { return bswap64(val); }

/* Convert between native CPU order and little-endian.
 * No-op on little-endian hosts, byte-swaps on big-endian hosts.
 */
static inline uint16_t cpu_to_le16(uint16_t val) { return WALLOS_BIG_ENDIAN ? bswap16(val) : val; }
static inline uint32_t cpu_to_le32(uint32_t val) { return WALLOS_BIG_ENDIAN ? bswap32(val) : val; }
static inline uint64_t cpu_to_le64(uint64_t val) { return WALLOS_BIG_ENDIAN ? bswap64(val) : val; }

static inline uint16_t le16_to_cpu(uint16_t val) { return cpu_to_le16(val); }
static inline uint32_t le32_to_cpu(uint32_t val) { return cpu_to_le32(val); }
static inline uint64_t le64_to_cpu(uint64_t val) { return cpu_to_le64(val); }

/* Convert between native CPU order and big-endian.
 * No-op on big-endian hosts, byte-swaps on little-endian hosts.
 */
static inline uint16_t cpu_to_be16(uint16_t val) { return WALLOS_BIG_ENDIAN ? val : bswap16(val); }
static inline uint32_t cpu_to_be32(uint32_t val) { return WALLOS_BIG_ENDIAN ? val : bswap32(val); }
static inline uint64_t cpu_to_be64(uint64_t val) { return WALLOS_BIG_ENDIAN ? val : bswap64(val); }

static inline uint16_t be16_to_cpu(uint16_t val) { return cpu_to_be16(val); }
static inline uint32_t be32_to_cpu(uint32_t val) { return cpu_to_be32(val); }
static inline uint64_t be64_to_cpu(uint64_t val) { return cpu_to_be64(val); }


// ------------------------------------------------------------------------------------------------
// Common field masks
// ------------------------------------------------------------------------------------------------

#define MASK_8_LOWER_HALF  ((uint8_t)  GENMASK(3, 0))   /* 0x0F */
#define MASK_8_UPPER_HALF  ((uint8_t)  GENMASK(7, 4))   /* 0xF0 */

#define MASK_16_LOWER_HALF ((uint16_t) GENMASK(7, 0))   /* 0x00FF */
#define MASK_16_UPPER_HALF ((uint16_t) GENMASK(15, 8))  /* 0xFF00 */

#define MASK_32_LOWER_HALF ((uint32_t) GENMASK(15, 0))  /* 0x0000FFFF */
#define MASK_32_UPPER_HALF ((uint32_t) GENMASK(31, 16)) /* 0xFFFF0000 */

#define MASK_64_LOWER_HALF ((uint64_t) GENMASK(31, 0))  /* 0x00000000FFFFFFFF */
#define MASK_64_UPPER_HALF ((uint64_t) GENMASK(63, 32)) /* 0xFFFFFFFF00000000 */

#define MASK_16_BYTE0 ((uint16_t) GENMASK(7, 0))    /* 0x00FF */
#define MASK_16_BYTE1 ((uint16_t) GENMASK(15, 8))   /* 0xFF00 */

#define MASK_16_BYTE0 ((uint16_t) GENMASK(7, 0))    /* 0x00FF */
#define MASK_16_BYTE1 ((uint16_t) GENMASK(15, 8))   /* 0xFF00 */

#define MASK_32_BYTE0 ((uint32_t) GENMASK(7, 0))    /* 0x000000FF */
#define MASK_32_BYTE1 ((uint32_t) GENMASK(15, 8))   /* 0x0000FF00 */
#define MASK_32_BYTE2 ((uint32_t) GENMASK(23, 16))  /* 0x00FF0000 */
#define MASK_32_BYTE3 ((uint32_t) GENMASK(31, 24))  /* 0xFF000000 */

#define MASK_64_BYTE0 ((uint64_t) GENMASK(7, 0))    /* 0x00000000000000FF */
#define MASK_64_BYTE1 ((uint64_t) GENMASK(15, 8))   /* 0x000000000000FF00 */
#define MASK_64_BYTE2 ((uint64_t) GENMASK(23, 16))  /* 0x0000000000FF0000 */
#define MASK_64_BYTE3 ((uint64_t) GENMASK(31, 24))  /* 0x00000000FF000000 */
#define MASK_64_BYTE4 ((uint64_t) GENMASK(39, 32))  /* 0x000000FF00000000 */
#define MASK_64_BYTE5 ((uint64_t) GENMASK(47, 40))  /* 0x0000FF0000000000 */
#define MASK_64_BYTE6 ((uint64_t) GENMASK(55, 48))  /* 0x00FF000000000000 */
#define MASK_64_BYTE7 ((uint64_t) GENMASK(63, 56))  /* 0xFF00000000000000 */


// ------------------------------------------------------------------------------------------------
// Fields
// ------------------------------------------------------------------------------------------------

/* Extracts a bitfield selected by mask and returns it right-justified.
 *
 * Example:
 * // Bits 4-5 contain 0b10
 * uint32_t mode = FIELD_GET(0x30, reg); // mode == 2
 */
#define FIELD_GET(mask, x) (((x) & (mask)) >> __builtin_ctzl(mask))

/* Prepares val for insertion into the bitfield selected by mask.
 *
 * Example: reg |= FIELD_PREP(0x30, 2); // produces 0x20
 */
#define FIELD_PREP(mask, val) (((unsigned long)(val) << __builtin_ctzl(mask)) & (mask))

/* Replaces the bitfield selected by mask with val.
 * Performs a read-modify-write. All other bits are preserved.
 *
 * Example: FIELD_WRITE(reg, 0x30, 2); // sets bits 4-5 to 0b10
 */
#define FIELD_WRITE(x, mask, val) MASK_WRITE(x, mask, FIELD_PREP(mask, val))

// ------------------------------------------------------------------------------------------------
// BCD conversion
// ------------------------------------------------------------------------------------------------

/* Converts an 8-bit packed BCD value */
static inline uint8_t bcd_to_bin8(uint8_t bcd) {
	return (uint8_t) (((bcd >> 4) * 10) + (bcd & 0x0F));
}

/* Converts an 8-bit binary value (0-99) to packed BCD (89 -> 0x89). */
static inline uint8_t bin_to_bcd8(uint8_t bin) {
	return (uint8_t) (((bin / 10) << 4) | (bin % 10));
}

/* Converts a 16-bit packed BCD value (four decimal digits, like 0x1234) to binary (1234). */
static inline uint16_t bcd_to_bin16(uint16_t bcd) {
	return (uint16_t) (((bcd >> 12) & 0xF) * 1000 +
		((bcd >> 8) & 0xF) * 100 +
		((bcd >> 4) & 0xF) * 10 +
		(bcd & 0xF));
}

/* Converts a 16-bit binary value (0-9999) to packed BCD (four decimal digits). */
static inline uint16_t bin_to_bcd16(uint16_t bin) {
	return (uint16_t) (((bin / 1000) << 12) |
		(((bin / 100) % 10) << 8) |
		(((bin / 10) % 10) << 4) |
		(bin % 10));
}


#endif /* ENDIAN_BITS_H */