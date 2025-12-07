#ifndef KTIME_H
#define KTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// CMOS Defines
#define CMOS_OUT_PORT 0x70
#define CMOS_IN_PORT  0x71
#define CMOS_SECONDS  0x00
#define CMOS_MINUTES  0x02
#define CMOS_HOURS    0x04
#define CMOS_WEEKDAY  0x06
#define CMOS_DAY      0x07
#define CMOS_MONTH    0x08
#define CMOS_YEAR     0x09
#define CMOS_CENTURY  0x32
#define CMOS_STATUS_A 0x0A
#define CMOS_STATUS_B 0x0B


	uint32_t get_system_msdos_time();
	void read_cmos_time(uint8_t* hours, uint8_t* minutes, uint8_t* seconds);
	void read_cmos_date(uint8_t* day, uint8_t* month, uint16_t* year);


#ifdef __cplusplus
}
#endif
#endif // KTIME_H