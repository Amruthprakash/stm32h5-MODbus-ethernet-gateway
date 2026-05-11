/**
 * @file    cli.h
 * @brief   UART command-line interface (debug port)
 *
 * Supported commands:
 *   stats   — print diagnostics counters
 *   tasks   — FreeRTOS task list and stack high-water marks
 *   heap    — free heap size
 *   packets — Ethernet packet counters
 *   errors  — error counters only
 *   uptime  — seconds since boot
 *   reset   — reset counters
 *   help    — list commands
 */
#ifndef CLI_H
#define CLI_H

/**
 * @brief  Initialise CLI (nothing to allocate — uses diag UART directly).
 */
void CLI_Init(void);

/**
 * @brief  CLITask body — blocks on debug UART input, parses commands.
 */
void CLI_Task(void *pvParameters);

#endif /* CLI_H */
