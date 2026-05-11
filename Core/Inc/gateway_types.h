/**
 * @file    gateway_types.h
 * @brief   Shared data types and inter-task message structures
 *
 * All queue payloads are defined here so every module speaks the same
 * language without tight coupling between layers.
 */
#ifndef GATEWAY_TYPES_H
#define GATEWAY_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "gateway_config.h"

/* =========================================================================
   Modbus frame message  (UartRxTask → ModbusParserTask)
   ========================================================================= */
typedef struct {
    uint8_t  data[GW_MODBUS_MAX_FRAME_LEN]; /**< Raw RTU frame incl. CRC       */
    uint16_t length;                         /**< Byte count                     */
    uint32_t timestamp_ms;                   /**< xTaskGetTickCount() at capture  */
} GW_ModbusFrameMsg_t;

/* =========================================================================
   Parsed Modbus event (ModbusParserTask → GatewayTask)
   ========================================================================= */
typedef enum {
    MB_EVT_SLAVE_REQUEST  = 0,  /**< Gateway acting as slave, got a request   */
    MB_EVT_MASTER_RESPONSE,     /**< Gateway acting as master, got a response */
    MB_EVT_CRC_ERROR,
    MB_EVT_TIMEOUT,
    MB_EVT_UNKNOWN_FUNC,
} GW_MbEventType_t;

typedef struct {
    GW_MbEventType_t type;
    uint8_t  slave_id;
    uint8_t  function_code;
    uint16_t start_addr;
    uint16_t quantity;
    uint8_t  payload[GW_MODBUS_MAX_FRAME_LEN];
    uint16_t payload_len;
} GW_MbEvent_t;

/* =========================================================================
   Ethernet packet message (GatewayTask ↔ EthRxTask / EthTxTask)
   ========================================================================= */
typedef struct {
    uint8_t  data[GW_ETH_MAX_FRAME_LEN];
    uint16_t length;
    uint32_t timestamp_ms;
} GW_EthPacket_t;

/* =========================================================================
   Log message (any task → DiagTask)
   ========================================================================= */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} GW_LogLevel_t;

typedef struct {
    GW_LogLevel_t level;
    char          msg[GW_LOG_MSG_MAX_LEN];
} GW_LogMsg_t;

/* =========================================================================
   Diagnostics counters (written by tasks, read by DiagTask / CLI)
   ========================================================================= */
typedef struct {
    /* Modbus */
    uint32_t mb_frames_rx;        /**< Valid Modbus frames received              */
    uint32_t mb_crc_errors;       /**< CRC failures on received frames           */
    uint32_t mb_timeouts;         /**< Inter-character / response timeouts       */
    uint32_t mb_unknown_func;     /**< Unrecognised function codes               */
    uint32_t mb_frames_tx;        /**< Modbus frames transmitted (master)        */
    /* Ethernet */
    uint32_t eth_rx_frames;       /**< Ethernet frames received                  */
    uint32_t eth_tx_frames;       /**< Ethernet frames transmitted               */
    uint32_t eth_rx_drops;        /**< RX frames dropped (queue full / too short)*/
    uint32_t eth_tx_errors;       /**< TX failures                               */
    /* UART */
    uint32_t uart_rx_overflow;    /**< UART RX ring-buffer overflows             */
    uint32_t uart_rx_bytes;       /**< Total UART bytes received                 */
    /* System */
    uint32_t uptime_seconds;      /**< Seconds since boot                        */
    uint32_t queue_overflow;      /**< Any inter-task queue full events          */
} GW_Diagnostics_t;

#endif /* GATEWAY_TYPES_H */
