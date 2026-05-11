/**
 * @file    mb_master.h
 * @brief   Modbus RTU Master — HAL-decoupled, RTOS-aware
 *
 * Ported from STM32F4 Modbus.c (Maslyakov / libmodbus based).
 * HAL_UART_Transmit() removed; caller retrieves the built frame via
 * MB_Master_GetTxFrame() and transmits it through the ETH gateway or RS-485.
 *
 * Usage sequence:
 *   1. MB_Master_Init()       — once at startup
 *   2. MB_Master_BuildQuery() — build frame into internal buffer
 *   3. MB_Master_GetTxFrame() — get pointer + length, then transmit
 *   4. Feed each received byte to MB_Master_ReceiveByte()
 *   5. When receive returns MB_MASTER_RX_COMPLETE → call MB_Master_ProcessResponse()
 *   6. Optionally call MB_Master_Reset() before next query
 */
#ifndef MB_MASTER_H
#define MB_MASTER_H

#include <stdint.h>
#include <stdbool.h>
#include "mb_frame.h"

/* =========================================================================
   Return codes
   ========================================================================= */
typedef enum {
    MB_MASTER_OK            =  0,
    MB_MASTER_ERR_PARAM     = -1,  /**< Bad input parameter                  */
    MB_MASTER_ERR_LEN       = -2,  /**< Requested count exceeds maximum       */
    MB_MASTER_ERR_FUNC      = -3,  /**< Unsupported function code             */
    MB_MASTER_ERR_SLAVE     = -4,  /**< Slave address mismatch in response    */
    MB_MASTER_ERR_CRC       = -5,  /**< CRC validation failed                 */
    MB_MASTER_ERR_EXCEPTION = -6,  /**< Slave returned Modbus exception       */
    MB_MASTER_ERR_TIMEOUT   = -7,  /**< No / partial response received        */
    MB_MASTER_ERR_DATA      = -8,  /**< Data length inconsistency in response */
    MB_MASTER_RX_COMPLETE   =  1,  /**< Response fully received, CRC OK       */
    MB_MASTER_RX_ONGOING    =  2,  /**< Still receiving bytes                 */
} MB_MasterResult_t;

/* =========================================================================
   Public API
   ========================================================================= */

/**
 * @brief  Initialise (or re-initialise) the master with a target slave.
 * @param  slave_id       Modbus slave address [1..247]
 * @param  function_code  Default function code for this session
 */
void MB_Master_Init(uint8_t slave_id, uint8_t function_code);

/**
 * @brief  Build a query frame from the supplied parameters.
 * @param  query  Pointer to query parameters struct (addr, count, data).
 * @return Expected response length (>0), or a negative MB_MasterResult_t on error.
 */
int32_t MB_Master_BuildQuery(const MB_MasterQuery_t *query);

/**
 * @brief  Obtain a pointer to the built TX frame and its length.
 * @param  out_buf  Receives pointer to internal frame buffer (read-only).
 * @param  out_len  Receives frame byte count.
 * @retval true   Frame is ready.
 * @retval false  No frame built yet.
 */
bool MB_Master_GetTxFrame(const uint8_t **out_buf, uint16_t *out_len);

/**
 * @brief  Feed one received byte into the response parser.
 * @param  byte  Received byte.
 * @return MB_MASTER_RX_ONGOING  — keep receiving.
 *         MB_MASTER_RX_COMPLETE — full response received; call ProcessResponse().
 *         negative              — fatal parse error.
 */
MB_MasterResult_t MB_Master_ReceiveByte(uint8_t byte);

/**
 * @brief  Validate and extract payload from a completed response.
 *
 * After calling this, raw register values are available via
 * MB_Master_GetResponseData().
 *
 * @return MB_MASTER_OK or negative error code.
 */
MB_MasterResult_t MB_Master_ProcessResponse(void);

/**
 * @brief  Access the validated payload (register data, coil bytes, etc.).
 * @param  out_data  Receives pointer to payload bytes (big-endian per Modbus).
 * @param  out_len   Receives payload byte count.
 */
void MB_Master_GetResponseData(const uint8_t **out_data, uint16_t *out_len);

/**
 * @brief  Reset internal RX buffers and parser state for the next transaction.
 */
void MB_Master_Reset(void);

#endif /* MB_MASTER_H */
