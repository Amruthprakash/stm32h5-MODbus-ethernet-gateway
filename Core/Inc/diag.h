/**
 * @file    diag.h
 * @brief   Diagnostics / statistics framework
 */
#ifndef DIAG_H
#define DIAG_H

#include <stdint.h>
#include "gateway_types.h"
#include "FreeRTOS.h"
#include "atomic.h"  /* FreeRTOS Atomic_Increment_u32 */

/* =========================================================================
   Global counters — written from multiple tasks / ISRs
   Use DIAG_Increment() for thread-safe atomic increment
   ========================================================================= */
extern GW_Diagnostics_t gDiag;

/**
 * @brief  Atomically increment a diagnostics counter by 1.
 * @param  counter  Pointer to a uint32_t field inside gDiag.
 */
static inline void DIAG_Increment(uint32_t *counter)
{
    Atomic_Increment_u32(counter);
}

/**
 * @brief  Initialise diagnostics (zero all counters, create task).
 */
void DIAG_Init(void);

/**
 * @brief  DiagTask body — periodically prints stats over debug UART.
 */
void DIAG_Task(void *pvParameters);

/**
 * @brief  Fill a string buffer with a one-line summary for the CLI.
 * @param  buf  Output buffer.
 * @param  size Buffer capacity.
 */
void DIAG_GetStatsString(char *buf, uint32_t size);

#endif /* DIAG_H */
