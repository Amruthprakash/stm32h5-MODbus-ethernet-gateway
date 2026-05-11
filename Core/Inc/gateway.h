/**
 * @file    gateway.h
 * @brief   Gateway translation layer — Modbus RTU ↔ Ethernet bridge
 *
 * This is the core of the application. It:
 *   1. Receives assembled Modbus frames from the UART RX / parser pipeline
 *   2. Wraps them in simple Ethernet frames (custom header) for forwarding
 *   3. Receives Ethernet frames from the network
 *   4. Unwraps and dispatches Modbus commands to the slave/master stack
 *
 * Simple framing protocol used over Ethernet (port 0x4D42 = 'MB'):
 *   [6 dst MAC][6 src MAC][2 EtherType=0x4D42][2 msg_type][2 payload_len][payload]
 *
 * This can be upgraded to Modbus TCP (port 502) in a future iteration.
 */
#ifndef GATEWAY_H
#define GATEWAY_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "gateway_types.h"

/* =========================================================================
   Custom EtherType for Modbus-over-Ethernet framing
   ========================================================================= */
#define GW_ETHERTYPE_MODBUS     0x4D42U   /**< 'MB' — not IANA assigned, for lab use */
#define GW_MSG_TYPE_RTU_FRAME   0x0001U   /**< Payload = raw Modbus RTU frame        */
#define GW_MSG_TYPE_RESPONSE    0x0002U   /**< Payload = slave response              */
#define GW_MSG_TYPE_DIAGNOSTICS 0x00FFU   /**< Payload = diagnostics snapshot        */

/* =========================================================================
   Inbound Ethernet queue handle (EthRxTask → GatewayTask)
   ========================================================================= */
extern QueueHandle_t xGatewayEthInQueue;

/* =========================================================================
   Public API
   ========================================================================= */

/**
 * @brief  Initialise the gateway layer — creates queues and registers the
 *         Modbus slave TX callback.
 */
void Gateway_Init(void);

/**
 * @brief  GatewayTask body — call from a dedicated FreeRTOS task.
 *
 * Blocks on xGatewayEthInQueue and xModbusEventQueue, then:
 *   - Modbus frame received → wrap and enqueue to xEthTxQueue
 *   - Ethernet frame received → unwrap and process via mb_slave / mb_master
 */
void Gateway_Task(void *pvParameters);

/**
 * @brief  Post a Modbus event to the gateway for forwarding.
 *         Called by ModbusParserTask after a valid frame is parsed.
 * @param  evt   Parsed event descriptor.
 */
void Gateway_PostModbusEvent(const GW_MbEvent_t *evt);

#endif /* GATEWAY_H */
