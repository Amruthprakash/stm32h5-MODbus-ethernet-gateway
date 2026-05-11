/**
 * @file    eth_gateway.h
 * @brief   Ethernet abstraction layer for the gateway
 *
 * Wraps the STM32H5 HAL ETH driver (from eth.c / main.c) behind a
 * clean queue-based API. No Modbus knowledge here — pure Ethernet I/O.
 *
 * Two RTOS tasks own this module:
 *   - EthRxTask: calls ETH_GW_ReadFrame() in a loop, posts to xEthRxQueue
 *   - EthTxTask: blocks on xEthTxQueue, calls ETH_GW_WriteFrame()
 */
#ifndef ETH_GATEWAY_H
#define ETH_GATEWAY_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "gateway_types.h"

/* =========================================================================
   Queue handles — created by ETH_GW_Init(), used by tasks
   ========================================================================= */
extern QueueHandle_t xEthRxQueue;   /**< EthRxTask → GatewayTask  */
extern QueueHandle_t xEthTxQueue;   /**< GatewayTask → EthTxTask  */

/* =========================================================================
   Public API
   ========================================================================= */

/**
 * @brief  Initialise HAL ETH, DMA descriptors, queues, and MAC filter.
 *         Call once before starting the RTOS scheduler.
 */
void ETH_GW_Init(void);

/**
 * @brief  Start Ethernet DMA (call after scheduler is running if needed,
 *         or just before — safe either way).
 */
void ETH_GW_Start(void);

/**
 * @brief  Transmit a single Ethernet frame.
 *         Blocks until HAL ETH transmit completes or timeout.
 * @param  buf   Frame data (14-byte header + payload).
 * @param  len   Total frame length [14..1514].
 * @retval true  Transmitted successfully.
 * @retval false HAL error or invalid length.
 */
bool ETH_GW_WriteFrame(const uint8_t *buf, uint16_t len);

/**
 * @brief  Read one Ethernet frame from the RX DMA pool.
 *         Non-blocking — returns false immediately if no frame is waiting.
 * @param  out_pkt   Filled on success.
 * @retval true   Frame available and copied.
 * @retval false  No frame ready.
 */
bool ETH_GW_ReadFrame(GW_EthPacket_t *out_pkt);

/**
 * @brief  EthRxTask body — call from a dedicated FreeRTOS task.
 *         Polls RX DMA and posts frames to xEthRxQueue.
 */
void ETH_GW_RxTask(void *pvParameters);

/**
 * @brief  EthTxTask body — call from a dedicated FreeRTOS task.
 *         Blocks on xEthTxQueue and transmits each packet.
 */
void ETH_GW_TxTask(void *pvParameters);

/* =========================================================================
   Diagnostic counters (updated internally, read by DiagTask / CLI)
   ========================================================================= */
typedef struct {
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t rx_drops;   /**< Queue full or frame too short */
    uint32_t tx_errors;
} ETH_GW_Stats_t;

const ETH_GW_Stats_t *ETH_GW_GetStats(void);

#endif /* ETH_GATEWAY_H */
