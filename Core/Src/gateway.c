/**
 * @file    gateway.c
 * @brief   Gateway translation layer — Modbus RTU ↔ Ethernet bridge
 *
 * Bridges the Modbus serial side and the Ethernet network side.
 * No HAL calls here — uses only mb_slave / mb_master APIs and eth_gateway APIs.
 */
#include <string.h>
#include "gateway.h"
#include "eth_gateway.h"
#include "mb_slave.h"
#include "mb_master.h"
#include "diag.h"
#include "gateway_config.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* =========================================================================
   Queue handles
   ========================================================================= */
QueueHandle_t xGatewayEthInQueue  = NULL;   /**< EthRxTask → GatewayTask   */
static QueueHandle_t xMbEventQueue = NULL;  /**< ParserTask → GatewayTask  */

/* =========================================================================
   Destination MAC for outgoing Ethernet frames
   Override with the peer's actual MAC (e.g. from ARP or config)
   ========================================================================= */
static const uint8_t s_dst_mac[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF }; /* broadcast */
static const uint8_t s_src_mac[6] = GW_ETH_MAC_ADDR;

/* =========================================================================
   Modbus slave TX callback — invoked by mb_slave when response is ready
   Wraps the RTU response in an Ethernet frame and enqueues for TX
   ========================================================================= */
static void prv_SlaveResponseCb(const uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;

    GW_EthPacket_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    uint8_t *p = pkt.data;

    /* Ethernet header */
    memcpy(p, s_dst_mac, 6); p += 6;
    memcpy(p, s_src_mac, 6); p += 6;

    /* Custom EtherType */
    *p++ = (uint8_t)(GW_ETHERTYPE_MODBUS >> 8);
    *p++ = (uint8_t)(GW_ETHERTYPE_MODBUS & 0xFF);

    /* Message type: response */
    *p++ = (uint8_t)(GW_MSG_TYPE_RESPONSE >> 8);
    *p++ = (uint8_t)(GW_MSG_TYPE_RESPONSE & 0xFF);

    /* Payload length */
    *p++ = (uint8_t)(len >> 8);
    *p++ = (uint8_t)(len & 0xFF);

    /* RTU response payload */
    if (len > GW_ETH_MAX_FRAME_LEN - 20U) len = (uint16_t)(GW_ETH_MAX_FRAME_LEN - 20U);
    memcpy(p, buf, len);
    p += len;

    pkt.length       = (uint16_t)(p - pkt.data);
    pkt.timestamp_ms = xTaskGetTickCount();

    xQueueSend(xEthTxQueue, &pkt, 0);

    DIAG_Increment(&gDiag.eth_tx_frames);
}

/* =========================================================================
   Forward a Modbus RTU frame over Ethernet
   ========================================================================= */
static void prv_ForwardModbusFrameToEth(const GW_MbEvent_t *evt)
{
    GW_EthPacket_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    uint8_t *p = pkt.data;
    uint16_t  pl_len = evt->payload_len;

    memcpy(p, s_dst_mac, 6); p += 6;
    memcpy(p, s_src_mac, 6); p += 6;

    *p++ = (uint8_t)(GW_ETHERTYPE_MODBUS >> 8);
    *p++ = (uint8_t)(GW_ETHERTYPE_MODBUS & 0xFF);

    *p++ = (uint8_t)(GW_MSG_TYPE_RTU_FRAME >> 8);
    *p++ = (uint8_t)(GW_MSG_TYPE_RTU_FRAME & 0xFF);

    *p++ = (uint8_t)(pl_len >> 8);
    *p++ = (uint8_t)(pl_len & 0xFF);

    if (pl_len > GW_ETH_MAX_FRAME_LEN - 20U)
        pl_len = (uint16_t)(GW_ETH_MAX_FRAME_LEN - 20U);
    memcpy(p, evt->payload, pl_len);
    p += pl_len;

    pkt.length       = (uint16_t)(p - pkt.data);
    pkt.timestamp_ms = xTaskGetTickCount();

    if (xQueueSend(xEthTxQueue, &pkt, 0) != pdTRUE)
        DIAG_Increment(&gDiag.queue_overflow);
    else
        DIAG_Increment(&gDiag.eth_tx_frames);
}

/* =========================================================================
   Process an inbound Ethernet frame — extract embedded Modbus RTU frame
   ========================================================================= */
static void prv_ProcessEthFrame(const GW_EthPacket_t *pkt)
{
    const uint8_t *p = pkt->data;

    if (pkt->length < 20U) return; /* too short for our custom header */

    /* Parse EtherType (offset 12..13) */
    uint16_t ethertype = ((uint16_t)p[12] << 8U) | p[13];
    if (ethertype != GW_ETHERTYPE_MODBUS) return; /* not our protocol */

    uint16_t msg_type  = ((uint16_t)p[14] << 8U) | p[15];
    uint16_t pl_len    = ((uint16_t)p[16] << 8U) | p[17];
    const uint8_t *payload = &p[18];

    if (pl_len > pkt->length - 18U) return; /* sanity check */

    if (msg_type == GW_MSG_TYPE_RTU_FRAME)
    {
        /* Dispatch to slave for processing (will call TX callback if addressed to us) */
        MB_Slave_ProcessFrame(payload, pl_len);
    }
    /* Future: handle GW_MSG_TYPE_RESPONSE for master mode acknowledgement */

    DIAG_Increment(&gDiag.eth_rx_frames);
}

/* =========================================================================
   Public API
   ========================================================================= */

void Gateway_Init(void)
{
    xGatewayEthInQueue = xQueueCreate(GW_ETH_RX_QUEUE_LEN, sizeof(GW_EthPacket_t));
    xMbEventQueue      = xQueueCreate(GW_MODBUS_FRAME_QUEUE_LEN, sizeof(GW_MbEvent_t));

    configASSERT(xGatewayEthInQueue != NULL);
    configASSERT(xMbEventQueue      != NULL);
}

void Gateway_PostModbusEvent(const GW_MbEvent_t *evt)
{
    if (xQueueSend(xMbEventQueue, evt, 0) != pdTRUE)
        DIAG_Increment(&gDiag.queue_overflow);
}

void Gateway_Task(void *pvParameters)
{
    (void)pvParameters;

    GW_EthPacket_t eth_pkt;
    GW_MbEvent_t   mb_evt;

    for (;;)
    {
        /* Service inbound Ethernet frames */
        if (xQueueReceive(xGatewayEthInQueue, &eth_pkt, 0) == pdTRUE)
        {
            prv_ProcessEthFrame(&eth_pkt);
        }

        /* Service Modbus events from the parser */
        if (xQueueReceive(xMbEventQueue, &mb_evt, pdMS_TO_TICKS(1)) == pdTRUE)
        {
            switch (mb_evt.type)
            {
            case MB_EVT_SLAVE_REQUEST:
                /* Slave already sent response via callback; also forward to Ethernet */
                prv_ForwardModbusFrameToEth(&mb_evt);
                break;

            case MB_EVT_MASTER_RESPONSE:
                /* Forward master response upstream to Ethernet */
                prv_ForwardModbusFrameToEth(&mb_evt);
                break;

            case MB_EVT_CRC_ERROR:
                DIAG_Increment(&gDiag.mb_crc_errors);
                break;

            case MB_EVT_TIMEOUT:
                DIAG_Increment(&gDiag.mb_timeouts);
                break;

            default:
                break;
            }
        }
    }
}
