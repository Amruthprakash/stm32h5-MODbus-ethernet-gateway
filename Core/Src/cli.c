/*
 * cli.c
 *
 *  Created on: May 11, 2026
 *      Author: LENOVO
 */


/**
 * @file    cli.c
 * @brief   Simple blocking CLI over debug UART (USART3 on STM32H5)
 */
#include <string.h>
#include <stdio.h>
#include "cli.h"
#include "diag.h"
#include "gateway_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32h5xx_hal.h"

extern UART_HandleTypeDef huart3;   /* Debug UART from generated code */

/* =========================================================================
   Private helpers
   ========================================================================= */
static void cli_puts(const char *s)
{
    HAL_UART_Transmit(&huart3, (const uint8_t *)s,
                      (uint16_t)strlen(s), 200);
}

static void cli_dispatch(const char *line)
{
    char buf[512];

    if (strncmp(line, "stats", 5) == 0 ||
        strncmp(line, "packets", 7) == 0)
    {
        DIAG_GetStatsString(buf, sizeof(buf));
        cli_puts(buf);
    }
    else if (strncmp(line, "errors", 6) == 0)
    {
        snprintf(buf, sizeof(buf),
            "CRC errors : %lu\r\nTimeouts   : %lu\r\nETH drops  : %lu\r\n"
            "ETH TX err : %lu\r\nUART ovf   : %lu\r\nQ overflow : %lu\r\n",
            gDiag.mb_crc_errors, gDiag.mb_timeouts,
            gDiag.eth_rx_drops, gDiag.eth_tx_errors,
            gDiag.uart_rx_overflow, gDiag.queue_overflow);
        cli_puts(buf);
    }
    else if (strncmp(line, "uptime", 6) == 0)
    {
        snprintf(buf, sizeof(buf), "Uptime: %lu s\r\n", gDiag.uptime_seconds);
        cli_puts(buf);
    }
    else if (strncmp(line, "heap", 4) == 0)
    {
        snprintf(buf, sizeof(buf), "Free heap: %u bytes\r\n",
                 (unsigned)xPortGetFreeHeapSize());
        cli_puts(buf);
    }
    else if (strncmp(line, "tasks", 5) == 0)
    {
#if configUSE_TRACE_FACILITY && configUSE_STATS_FORMATTING_FUNCTIONS
        vTaskList(buf);
        cli_puts("\r\nTask Name       State Prio Stack Num\r\n");
        cli_puts("-----------------------------------\r\n");
        cli_puts(buf);
#else
        cli_puts("Enable configUSE_TRACE_FACILITY in FreeRTOSConfig.h\r\n");
#endif
    }
    else if (strncmp(line, "reset", 5) == 0)
    {
        memset(&gDiag, 0, sizeof(gDiag));
        cli_puts("Counters reset.\r\n");
    }
    else if (strncmp(line, "help", 4) == 0 || line[0] == '?')
    {
        cli_puts(
            "Commands: stats | tasks | heap | packets | errors | uptime | reset | help\r\n");
    }
    else if (line[0] != '\0')
    {
        cli_puts("Unknown command. Type 'help'.\r\n");
    }
}

/* =========================================================================
   Public API
   ========================================================================= */

void CLI_Init(void) { /* nothing */ }

void CLI_Task(void *pvParameters)
{
    (void)pvParameters;

    char    line[GW_CLI_LINE_MAX_LEN];
    uint8_t rx_byte;
    uint8_t idx = 0;

    cli_puts("\r\n" GW_CLI_PROMPT);

    for (;;)
    {
        /* Blocking receive — 1 byte at a time */
        if (HAL_UART_Receive(&huart3, &rx_byte, 1, portMAX_DELAY) != HAL_OK)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* Echo */
        HAL_UART_Transmit(&huart3, &rx_byte, 1, 10);

        if (rx_byte == '\r' || rx_byte == '\n')
        {
            line[idx] = '\0';
            cli_puts("\r\n");
            cli_dispatch(line);
            idx = 0;
            cli_puts(GW_CLI_PROMPT);
        }
        else if (rx_byte == '\b' || rx_byte == 0x7FU)
        {
            if (idx > 0) idx--;
        }
        else if (idx < GW_CLI_LINE_MAX_LEN - 1U)
        {
            line[idx++] = (char)rx_byte;
        }
    }
}
