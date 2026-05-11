/**
 * @file    mb_slave.h
 * @brief   Modbus RTU Slave — HAL-decoupled, callback-driven
 *
 * Ported from STM32F4 Modbus_RTU.c. Removed ModemSendData() direct HAL
 * dependency. Instead, when a response is ready the caller-supplied
 * TX callback is invoked. Register storage is application-managed.
 *
 * Supported function codes: 03, 04 (read regs), 06 (write single), 16 (write multi).
 */
#ifndef MB_SLAVE_H
#define MB_SLAVE_H

#include <stdint.h>
#include <stdbool.h>
#include "mb_frame.h"

/* =========================================================================
   TX callback type — called by ProcessFrame() when a response is ready.
   Implementer should queue/DMA-transmit the bytes.
   ========================================================================= */
typedef void (*MB_Slave_TxCallback_t)(const uint8_t *buf, uint16_t len, void *ctx);

/* =========================================================================
   Register bank — application fills these before calling MB_Slave_Init
   ========================================================================= */
typedef struct {
    uint16_t *holding_regs;     /**< Pointer to holding register array         */
    uint16_t  holding_count;    /**< Number of holding registers               */
    uint16_t  holding_start;    /**< First register address (usually 0)        */
} MB_Slave_RegBank_t;

/* =========================================================================
   Public API
   ========================================================================= */

/**
 * @brief  Initialise the slave.
 * @param  slave_id   Modbus slave address this device responds to [1..247].
 * @param  regs       Register bank descriptor.
 * @param  tx_cb      Callback invoked when a response frame is ready.
 * @param  tx_ctx     Opaque context pointer passed back through tx_cb.
 */
void MB_Slave_Init(uint8_t slave_id,
                   const MB_Slave_RegBank_t *regs,
                   MB_Slave_TxCallback_t tx_cb,
                   void *tx_ctx);

/**
 * @brief  Process a received RTU frame (complete, including CRC bytes).
 *
 * Call this from ModbusParserTask after a frame boundary is detected.
 * If the frame is addressed to this slave and is valid, a response is
 * generated and passed to the registered TX callback.
 *
 * @param  frame   Pointer to raw frame bytes.
 * @param  len     Total frame length (includes 2 CRC bytes).
 * @retval > 0     Response length (frame was processed and response sent).
 * @retval   0     Frame not addressed to this slave — silently ignored.
 * @retval < 0     Error (CRC fail, illegal data, etc.).
 */
int32_t MB_Slave_ProcessFrame(const uint8_t *frame, uint16_t len);

/**
 * @brief  Direct write to a holding register from the application layer.
 *         Useful for the Gateway layer to push Ethernet-sourced data.
 */
bool MB_Slave_WriteRegister(uint16_t addr, uint16_t value);

/**
 * @brief  Direct read from a holding register by the application layer.
 */
bool MB_Slave_ReadRegister(uint16_t addr, uint16_t *out_value);

#endif /* MB_SLAVE_H */
