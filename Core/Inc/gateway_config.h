/**
 * @file    gateway_config.h
 * @brief   Compile-time configuration for STM32H5 Industrial Ethernet Gateway
 *
 * Central place for all tunable parameters. Adjust before building.
 */
#ifndef GATEWAY_CONFIG_H
#define GATEWAY_CONFIG_H

/* =========================================================================
   UART / RS-485 (Modbus physical layer)
   ========================================================================= */
#define GW_MODBUS_UART_INSTANCE     USART1       /**< STM32H5 USART for RS-485 */
#define GW_MODBUS_BAUD              115200U
#define GW_MODBUS_RX_DMA_STREAM     DMA1_Stream0 /**< Optional DMA stream       */

/** Inter-frame silence = 3.5 char times in ms (ceil).
 *  At 9600 baud → ~4 ms.  At 115200 baud → ~1 ms.           */
#define GW_MODBUS_FRAME_TIMEOUT_MS  5U

/** RS-485 direction control GPIO */
#define GW_RS485_DE_PORT            GPIOA
#define GW_RS485_DE_PIN             GPIO_PIN_8

/* =========================================================================
   Debug / CLI UART
   ========================================================================= */
#define GW_DEBUG_UART_INSTANCE      USART3
#define GW_DEBUG_BAUD               115200U
#define GW_CLI_PROMPT               "gw> "

/* =========================================================================
   Modbus Slave identity
   ========================================================================= */
#define GW_MB_SLAVE_ID              1U
#define GW_MB_HOLDING_REG_START     0U
#define GW_MB_HOLDING_REG_COUNT     100U

/* =========================================================================
   Queue sizes (number of elements, NOT bytes)
   ========================================================================= */
#define GW_UART_BYTE_QUEUE_LEN      256U   /**< Raw bytes ISR → RX task        */
#define GW_MODBUS_FRAME_QUEUE_LEN   8U     /**< Assembled frames → parser task  */
#define GW_ETH_TX_QUEUE_LEN         4U     /**< Packets waiting for ETH TX      */
#define GW_ETH_RX_QUEUE_LEN         4U     /**< Received ETH frames → gateway   */
#define GW_LOG_QUEUE_LEN            16U    /**< Log messages → diag task        */

/* =========================================================================
   Buffer limits
   ========================================================================= */
#define GW_MODBUS_MAX_FRAME_LEN     256U   /**< RTU max = 256 bytes             */
#define GW_ETH_MAX_FRAME_LEN        1514U  /**< 14-byte header + 1500 payload   */
#define GW_LOG_MSG_MAX_LEN          80U    /**< Max chars per log message        */
#define GW_CLI_LINE_MAX_LEN         64U    /**< Max CLI input line length        */

/* =========================================================================
   Task stack sizes (in 32-bit words)
   ========================================================================= */
#define GW_STACK_UART_RX            256U
#define GW_STACK_MODBUS_PARSER      512U
#define GW_STACK_GATEWAY            512U
#define GW_STACK_ETH_RX             512U
#define GW_STACK_ETH_TX             256U
#define GW_STACK_DIAG               256U
#define GW_STACK_CLI                512U
#define GW_STACK_WATCHDOG           128U

/* =========================================================================
   Task priorities (higher number = higher priority in FreeRTOS)
   ========================================================================= */
#define GW_PRIO_WATCHDOG            (configMAX_PRIORITIES - 1U)
#define GW_PRIO_UART_RX             (configMAX_PRIORITIES - 2U)
#define GW_PRIO_ETH_RX              (configMAX_PRIORITIES - 2U)
#define GW_PRIO_MODBUS_PARSER       (configMAX_PRIORITIES - 3U)
#define GW_PRIO_ETH_TX              (configMAX_PRIORITIES - 3U)
#define GW_PRIO_GATEWAY             (configMAX_PRIORITIES - 4U)
#define GW_PRIO_CLI                 (configMAX_PRIORITIES - 5U)
#define GW_PRIO_DIAG                (configMAX_PRIORITIES - 6U)

/* =========================================================================
   Watchdog
   ========================================================================= */
#define GW_WATCHDOG_KICK_MS         500U   /**< Pet IWDG every 500 ms           */
#define GW_WATCHDOG_TIMEOUT_MS      2000U  /**< IWDG window                     */

/* =========================================================================
   Ethernet / MAC
   ========================================================================= */
#define GW_ETH_MAC_ADDR             { 0x00, 0x80, 0xE1, 0x00, 0x00, 0x01 }
#define GW_ETH_RX_DESC_CNT          4U
#define GW_ETH_TX_DESC_CNT          4U

/* =========================================================================
   Diagnostics
   ========================================================================= */
#define GW_DIAG_REPORT_PERIOD_MS    10000U /**< Print stats every 10 s           */

#endif /* GATEWAY_CONFIG_H */
