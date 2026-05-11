/**
 * @file    eth_gateway.c
 * @brief   Ethernet abstraction layer — STM32H5 HAL ETH wrapper
 *
 * Ported and refactored from:
 *   - eth.c       (MX_ETH_Init, HAL_ETH_MspInit)
 *   - main.c      (ETH callback functions, buffer pool management)
 *
 * Key changes from original:
 *   - Removed direct USB coupling (usb_tx_ready flags moved to gateway.c)
 *   - Buffer pool made thread-safe via mutex
 *   - RX/TX exposed through RTOS queues
 *   - ETH_GW_RxTask / ETH_GW_TxTask replace the super-loop polling
 */
#include <string.h>
#include "eth_gateway.h"
#include "eth.h"          /* HAL ETH handle and MX_ETH_Init() */
#include "gateway_config.h"
#include "diag.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* =========================================================================
   External HAL handle (defined in eth.c / generated code)
   ========================================================================= */
extern ETH_HandleTypeDef heth;

/* =========================================================================
   Queue handles — exported in header
   ========================================================================= */
QueueHandle_t xEthRxQueue = NULL;
QueueHandle_t xEthTxQueue = NULL;

/* =========================================================================
   RX buffer pool (mirrors original rx_buffer / rx_buf_free design)
   ========================================================================= */
static uint8_t  s_rx_pool[GW_ETH_RX_DESC_CNT][GW_ETH_MAX_FRAME_LEN];
static uint8_t  s_rx_free[GW_ETH_RX_DESC_CNT]; /**< 1 = slot is free */
static SemaphoreHandle_t s_rx_pool_mutex = NULL;

/* TX scratch buffer protected by mutex */
static uint8_t  s_tx_scratch[GW_ETH_MAX_FRAME_LEN];
static SemaphoreHandle_t s_tx_mutex = NULL;
static volatile uint8_t  s_eth_tx_busy = 0;

/* Frame length captured in HAL link callback */
static volatile uint16_t s_rx_pending_len = 0;

/* Statistics */
static ETH_GW_Stats_t s_stats;

/* =========================================================================
   Private helpers
   ========================================================================= */

static int32_t prv_FindSlot(const uint8_t *buf)
{
    for (uint32_t i = 0; i < GW_ETH_RX_DESC_CNT; i++)
        if (s_rx_pool[i] == buf) return (int32_t)i;
    return -1;
}

static uint8_t *prv_AllocSlot(void)
{
    for (uint32_t i = 0; i < GW_ETH_RX_DESC_CNT; i++)
    {
        if (s_rx_free[i])
        {
            s_rx_free[i] = 0;
            return s_rx_pool[i];
        }
    }
    return NULL;
}

/* =========================================================================
   Public API
   ========================================================================= */

void ETH_GW_Init(void)
{
    /* Mark all pool slots free */
    for (uint32_t i = 0; i < GW_ETH_RX_DESC_CNT; i++)
        s_rx_free[i] = 1U;

    /* Create RTOS primitives */
    xEthRxQueue   = xQueueCreate(GW_ETH_RX_QUEUE_LEN, sizeof(GW_EthPacket_t));
    xEthTxQueue   = xQueueCreate(GW_ETH_TX_QUEUE_LEN, sizeof(GW_EthPacket_t));
    s_rx_pool_mutex = xSemaphoreCreateMutex();
    s_tx_mutex      = xSemaphoreCreateMutex();

    configASSERT(xEthRxQueue   != NULL);
    configASSERT(xEthTxQueue   != NULL);
    configASSERT(s_rx_pool_mutex != NULL);
    configASSERT(s_tx_mutex      != NULL);

    /* HAL peripheral init (from eth.c MX_ETH_Init) */
    MX_ETH_Init();

    /* Set MAC promiscuous so gateway can forward any frame */
    ETH_MACFilterConfigTypeDef filter = {0};
    filter.PromiscuousMode  = ENABLE;
    filter.PassAllMulticast = ENABLE;
    HAL_ETH_SetMACFilterConfig(&heth, &filter);

    memset(&s_stats, 0, sizeof(s_stats));
}

void ETH_GW_Start(void)
{
    HAL_ETH_Start_IT(&heth);
}

bool ETH_GW_WriteFrame(const uint8_t *buf, uint16_t len)
{
    if (!buf || len < 14U || len > GW_ETH_MAX_FRAME_LEN) return false;

    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;

    /* Wait for previous TX to finish (HAL busy flag) */
    uint32_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(100);
    while (s_eth_tx_busy && (xTaskGetTickCount() < deadline))
        vTaskDelay(1);

    if (s_eth_tx_busy)
    {
        xSemaphoreGive(s_tx_mutex);
        s_stats.tx_errors++;
        return false;
    }

    memcpy(s_tx_scratch, buf, len);
    if (len < 60U) { memset(s_tx_scratch + len, 0, 60U - len); len = 60U; }

    ETH_BufferTypeDef txbuf = {
        .buffer = s_tx_scratch,
        .len    = len,
        .next   = NULL
    };
    ETH_TxPacketConfigTypeDef cfg = {0};
    cfg.Length      = len;
    cfg.TxBuffer    = &txbuf;
    cfg.Attributes  = ETH_TX_PACKETS_FEATURES_CRCPAD;
    cfg.CRCPadCtrl  = ETH_CRC_PAD_INSERT;

    s_eth_tx_busy = 1U;
    bool ok = (HAL_ETH_Transmit_IT(&heth, &cfg) == HAL_OK);
    if (!ok)
    {
        s_eth_tx_busy = 0U;
        s_stats.tx_errors++;
    }
    else
    {
        s_stats.tx_frames++;
    }

    xSemaphoreGive(s_tx_mutex);
    return ok;
}

bool ETH_GW_ReadFrame(GW_EthPacket_t *out_pkt)
{
    /* Frames are posted from the HAL RX callback — nothing to do here
       except drain the queue (called by EthRxTask). */
    return (xQueueReceive(xEthRxQueue, out_pkt, 0) == pdTRUE);
}

/* =========================================================================
   Task bodies
   ========================================================================= */

void ETH_GW_RxTask(void *pvParameters)
{
    (void)pvParameters;
    GW_EthPacket_t pkt;

    for (;;)
    {
        /* Block until a frame arrives in the rx queue (posted by ISR callback) */
        if (xQueueReceive(xEthRxQueue, &pkt, portMAX_DELAY) == pdTRUE)
        {
            /* Re-post to the gateway's inbound queue for processing */
            extern QueueHandle_t xGatewayEthInQueue;
            if (xQueueSend(xGatewayEthInQueue, &pkt, 0) != pdTRUE)
                s_stats.rx_drops++;
        }
    }
}

void ETH_GW_TxTask(void *pvParameters)
{
    (void)pvParameters;
    GW_EthPacket_t pkt;

    for (;;)
    {
        if (xQueueReceive(xEthTxQueue, &pkt, portMAX_DELAY) == pdTRUE)
        {
            ETH_GW_WriteFrame(pkt.data, pkt.length);
        }
    }
}

const ETH_GW_Stats_t *ETH_GW_GetStats(void)
{
    return &s_stats;
}

/* =========================================================================
   HAL ETH Callbacks — run in ISR context (keep short!)
   ========================================================================= */

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
    /* Called from ISR: assign a free pool slot to the DMA descriptor */
    *buff = prv_AllocSlot();
    /* If NULL, HAL will skip this frame — overflow counted in RxCplt */
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd,
                             uint8_t *buff, uint16_t length)
{
    *pStart = buff;
    *pEnd   = buff;
    s_rx_pending_len = length;
}

void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *heth_h)
{
    void    *appBuf = NULL;
    BaseType_t woken = pdFALSE;

    while (HAL_ETH_ReadData(heth_h, &appBuf) == HAL_OK)
    {
        uint8_t  *frame = (uint8_t *)appBuf;
        uint16_t  len   = s_rx_pending_len;
        int32_t   slot  = prv_FindSlot(frame);

        if (frame && len >= 14U && len <= GW_ETH_MAX_FRAME_LEN)
        {
            GW_EthPacket_t pkt;
            memcpy(pkt.data, frame, len);
            pkt.length       = len;
            pkt.timestamp_ms = xTaskGetTickCountFromISR();

            if (xQueueSendFromISR(xEthRxQueue, &pkt, &woken) != pdTRUE)
                s_stats.rx_drops++;
            else
                s_stats.rx_frames++;
        }

        if (slot >= 0) s_rx_free[slot] = 1U;
    }

    portYIELD_FROM_ISR(woken);
}

void HAL_ETH_TxCpltCallback(ETH_HandleTypeDef *heth_h)
{
    HAL_ETH_ReleaseTxPacket(heth_h);
    s_eth_tx_busy = 0U;
}
