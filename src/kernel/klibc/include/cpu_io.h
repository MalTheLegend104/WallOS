#ifndef CPU_IO_H
#define CPU_IO_H

// ------------------------------------------------------------------------------------------------
// This is meant to serve as a simple header for doing CPU port I/O operations.
// This should *probably* be with the rest of the x86 specific code, but for now it's here.
// ------------------------------------------------------------------------------------------------

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

	// read an 8 bit value from a port
	static inline uint8_t inb(uint16_t port) {
		uint8_t ret;
		__asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
		return ret;
	}

	// Read a 16-bit value from a port
	static inline uint16_t inw(uint16_t port) {
		uint16_t ret;
		__asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
		return ret;
	}

	// Read a 32-bit value from a port
	static inline uint32_t inl(uint16_t port) {
		uint32_t ret;
		__asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
		return ret;
	}

	static inline void outb(uint16_t port, uint8_t val) {
		__asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
	}
	static inline void outw(uint16_t port, uint16_t val) {
		__asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
	}
	static inline void outl(uint16_t port, uint32_t val) {
		__asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
	}

	// This really belongs elsewhere, but whatever
	static inline void i8042_flush(void) {
		int timeout = 1000;
		while ((inb(0x64) & 0x01) && timeout--) {
			inb(0x60); // discard byte
		}

		outb(0x64, 0xAE);
	}

#ifdef __cplusplus
}
#endif
#endif // CPU_IO_H