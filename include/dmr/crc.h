/**
 * @file
 * @brief Cyclic Redundancy Checks.
 */
#ifndef _DMR_CRC_H
#define _DMR_CRC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

#define DMR_CRC8_MASK_VOICE_PI              (0x69)
#define DMR_CRC8_MASK_VOICE_LC              (0x96)
#define DMR_CRC8_MASK_TERMINATOR_WITH_LC    (0x99)

extern void dmr_crc9(uint16_t *crc, uint8_t byte, uint8_t bitlen);
extern void dmr_crc9_finish(uint16_t *crc, uint8_t bitlen);
extern void dmr_crc16(uint16_t *crc, uint8_t byte);
extern void dmr_crc16_finish(uint16_t *crc);
extern void dmr_crc32(uint32_t *crc, uint8_t byte);
extern void dmr_crc32_finish(uint32_t *crc);

#ifdef __cplusplus
}
#endif

#endif // _DMR_CRC_H
