/**
 * @file    mb_frame.h
 * @brief   Modbus RTU frame definitions shared by Master and Slave
 *
 * Both the original F4 Master (Modbus.c) and Slave (Modbus_RTU.c) used
 * private structs with similar layout. Unified here.
 */
#ifndef MB_FRAME_H
#define MB_FRAME_H

#include <stdint.h>
#include "gateway_config.h"

/* =========================================================================
   Modbus Function Codes
   ========================================================================= */
#define MB_FC_READ_COILS              0x01U
#define MB_FC_READ_DISCRETE_INPUTS    0x02U
#define MB_FC_READ_HOLDING_REGS       0x03U
#define MB_FC_READ_INPUT_REGS         0x04U
#define MB_FC_WRITE_SINGLE_COIL       0x05U
#define MB_FC_WRITE_SINGLE_REG        0x06U
#define MB_FC_WRITE_MULTIPLE_COILS    0x0FU
#define MB_FC_WRITE_MULTIPLE_REGS     0x10U
#define MB_FC_EXCEPTION_MASK          0x80U

/* =========================================================================
   Modbus Exception Codes
   ========================================================================= */
#define MB_EX_ILLEGAL_FUNCTION        0x01U
#define MB_EX_ILLEGAL_DATA_ADDRESS    0x02U
#define MB_EX_ILLEGAL_DATA_VALUE      0x03U
#define MB_EX_SLAVE_DEVICE_FAILURE    0x04U

/* =========================================================================
   Frame limits
   ========================================================================= */
#define MB_RTU_MIN_FRAME_LEN          4U    /**< Addr + FC + CRC (2 bytes)     */
#define MB_RTU_MAX_FRAME_LEN          GW_MODBUS_MAX_FRAME_LEN
#define MB_CHECKSUM_LEN               2U
#define MB_HEADER_LEN                 2U    /**< SlaveAddr + FunctionCode       */

/* =========================================================================
   Register limits
   ========================================================================= */
#define MB_MAX_REGISTERS              125U  /**< Per spec: max regs per request */
#define MB_MAX_COILS                  2000U

/* =========================================================================
   Raw RTU frame overlay
   Allows direct field access on a received byte buffer.
   IMPORTANT: only valid after verifying CRC.
   ========================================================================= */
typedef struct __attribute__((packed)) {
    uint8_t  slave_addr;
    uint8_t  function_code;
    uint8_t  data[MB_RTU_MAX_FRAME_LEN - MB_HEADER_LEN - MB_CHECKSUM_LEN];
} MB_RtuFrame_t;

/* =========================================================================
   Master query build parameters (maps from original __MB_QUERY_BUILD)
   ========================================================================= */
typedef struct {
    uint16_t        addr;       /**< Start address / coil address             */
    uint16_t        count;      /**< Number of registers / coils              */
    const uint8_t  *data;       /**< Pointer to write data (FC 0x0F/0x10)     */
} MB_MasterQuery_t;

/* =========================================================================
   Master internal state (maps from original __MB_QUERY_TOOLS)
   ========================================================================= */
typedef struct {
    uint8_t  slave_id;
    uint8_t  function_code;
    uint8_t  tx_buf[MB_RTU_MAX_FRAME_LEN];
    uint16_t tx_len;
    uint16_t expected_rx_len;   /**< Computed from function + register count  */
} MB_MasterState_t;

/* =========================================================================
   Master response (maps from original __MB_ANSW_READY_DATA)
   ========================================================================= */
typedef struct {
    uint8_t  buf[MB_RTU_MAX_FRAME_LEN];
    uint16_t rx_len;            /**< Bytes accumulated so far                 */
    uint8_t  exception_code;    /**< Non-zero if slave returned exception      */
    uint8_t  parse_state;       /**< Internal parser state machine            */
} MB_MasterResponse_t;

#endif /* MB_FRAME_H */
