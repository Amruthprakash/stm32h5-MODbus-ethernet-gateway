/**
 * @file    mb_crc16.h
 * @brief   Modbus RTU CRC-16 (table-driven, identical for Master and Slave)
 *
 * Both the original STM32F4 Master (Modbus.c) and Slave (Modbus_RTU.c) used
 * identical CRC tables. This single module replaces both copies.
 */
#ifndef MB_CRC16_H
#define MB_CRC16_H

#include <stdint.h>

/**
 * @brief  Compute Modbus CRC-16 over a byte buffer.
 * @param  buf   Pointer to data (must not be NULL).
 * @param  len   Number of bytes to process.
 * @return 16-bit CRC, little-endian byte order as Modbus expects
 *         (CRC_lo at lower address, CRC_hi at higher address).
 */
uint16_t MB_CRC16(const uint8_t *buf, uint32_t len);

/**
 * @brief  Validate the trailing CRC-16 of a received RTU frame.
 * @param  buf     Pointer to the complete frame (including the 2 CRC bytes).
 * @param  total   Total frame length including CRC bytes.
 * @retval true    CRC matches.
 * @retval false   CRC mismatch — frame is corrupt.
 */
bool MB_CRC16_Verify(const uint8_t *buf, uint32_t total);

/**
 * @brief  Append a CRC-16 to the end of a frame being built.
 * @param  buf     Pointer to buffer; bytes [0 .. len-1] are the frame data.
 * @param  len     Number of data bytes (NOT including the 2 CRC bytes).
 *                 The function writes CRC_lo to buf[len] and CRC_hi to buf[len+1].
 * @note   Caller must ensure buf has at least (len + 2) bytes allocated.
 */
void MB_CRC16_Append(uint8_t *buf, uint32_t len);

#endif /* MB_CRC16_H */
