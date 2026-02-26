#ifndef WALLOS_LAPIC_H
#define WALLOS_LAPIC_H

#include <stdint.h>

static inline uint32_t lapic_read(uint32_t offset);
static inline void lapic_write(uint32_t offset, uint32_t value);

void set_lapic_base(uint64_t* base);

#endif // WALLOS_LAPIC_H