/** 
 * @file crc32.h
 * @author Malcolm
 * @brief 
 * @version 
 * @date 7/5/2026
 */
#ifndef WDM_MOCK_CRC32_H
#define WDM_MOCK_CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


uint32_t crc32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif
#endif //WDM_MOCK_CRC32_H
