/*
 * diag.c
 *
 *  Created on: May 11, 2026
 *      Author: LENOVO
 */


/**
 * @file    diag.c
 * @brief   Diagnostics / statistics implementation
 */
#include <string.h>
#include <stdio.h>
#include "diag.h"
#include "gateway_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "usart.h"

/* Global diagnostics struct — zeroed at startup */
GW_Diagnostics_t gDiag;

void DIAG_Init(void)
{
    memset(&gDiag, 0, sizeof(gDiag));
}

void DIAG_GetStatsString(char *buf, uint32_t size)
{
    snprintf(buf, size,
        "\r\n=== Gateway Diagnostics ===\r\n"
        " Uptime       : %lu s\r\n"
        " MB frames RX : %lu\r\n"
        " MB frames TX : %lu\r\n"
        " MB CRC errs  : %lu\r\n"
        " MB timeouts  : %lu\r\n"
        " ETH RX frames: %lu\r\n"
        " ETH TX frames: %lu\r\n"
        " ETH RX drops : %lu\r\n"
        " ETH TX errors: %lu\r\n"
        " UART overflow: %lu\r\n"
        " Queue OVF    : %lu\r\n"
        "===========================\r\n",
        gDiag.uptime_seconds,
        gDiag.mb_frames_rx,
        gDiag.mb_frames_tx,
        gDiag.mb_crc_errors,
        gDiag.mb_timeouts,
        gDiag.eth_rx_frames,
        gDiag.eth_tx_frames,
        gDiag.eth_rx_drops,
        gDiag.eth_tx_errors,
        gDiag.uart_rx_overflow,
        gDiag.queue_overflow
    );
}

void DIAG_Task(void *pvParameters)
{
    (void)pvParameters;
    char buf[512];
    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(GW_DIAG_REPORT_PERIOD_MS));

        gDiag.uptime_seconds += GW_DIAG_REPORT_PERIOD_MS / 1000U;

        DIAG_GetStatsString(buf, sizeof(buf));

        /* Output via debug UART — use HAL_UART_Transmit (non-blocking
           OK here because this is a low-priority task) */

        HAL_UART_Transmit(&huart3, (uint8_t *)buf,
                          (uint16_t)strlen(buf), 200);
    }
}
