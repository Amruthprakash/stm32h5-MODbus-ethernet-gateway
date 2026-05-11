/*
 * mb_slave.c
 *
 *  Created on: May 11, 2026
 *      Author: LENOVO
 */
/**
 * @file    mb_slave.c
 * @brief   Modbus RTU Slave — HAL-decoupled, callback-driven
 *
 * Ported from STM32F4 Modbus_RTU.c (FC 03/04/06/16).
 * ModemSendData() replaced by registered TX callback.
 * CRC replaced with shared mb_crc16 module.
 * Direct HAL dependency fully removed.
 */
#include <string.h>
#include "mb_slave.h"
#include "mb_crc16.h"

/* =========================================================================
   Private state
   ========================================================================= */
static uint8_t                s_slave_id;
static MB_Slave_RegBank_t     s_regs;
static MB_Slave_TxCallback_t  s_tx_cb;
static void                  *s_tx_ctx;

static uint8_t s_resp_buf[MB_RTU_MAX_FRAME_LEN];

/* =========================================================================
   Private helpers — register access
   ========================================================================= */

static bool prv_ReadRegisters(uint16_t start, uint16_t count, uint8_t *out)
{
    if ((start < s_regs.holding_start) ||
        ((uint32_t)start + count > (uint32_t)s_regs.holding_start + s_regs.holding_count))
        return false;

    uint16_t offset = start - s_regs.holding_start;
    for (uint16_t i = 0; i < count; i++)
    {
        uint16_t val = s_regs.holding_regs[offset + i];
        out[i * 2U]       = (uint8_t)(val >> 8U);   /* Modbus big-endian */
        out[i * 2U + 1U]  = (uint8_t)(val & 0xFFU);
    }
    return true;
}

static bool prv_WriteRegisters(uint16_t start, uint16_t count, const uint8_t *src)
{
    if ((start < s_regs.holding_start) ||
        ((uint32_t)start + count > (uint32_t)s_regs.holding_start + s_regs.holding_count))
        return false;

    uint16_t offset = start - s_regs.holding_start;
    for (uint16_t i = 0; i < count; i++)
    {
        uint16_t val = ((uint16_t)src[i * 2U] << 8U) | src[i * 2U + 1U];
        s_regs.holding_regs[offset + i] = val;
    }
    return true;
}

static void prv_SendException(uint8_t slave, uint8_t fc, uint8_t ex_code)
{
    s_resp_buf[0] = slave;
    s_resp_buf[1] = fc | MB_FC_EXCEPTION_MASK;
    s_resp_buf[2] = ex_code;
    MB_CRC16_Append(s_resp_buf, 3U);
    if (s_tx_cb) s_tx_cb(s_resp_buf, 5U, s_tx_ctx);
}

/* =========================================================================
   FC 03 / 04: Read Holding / Input Registers
   ========================================================================= */
static int32_t prv_FC03_04(const uint8_t *req, uint16_t req_len)
{
    if (req_len != 8U)
    {
        prv_SendException(req[0], req[1], MB_EX_ILLEGAL_DATA_VALUE);
        return -1;
    }

    uint16_t start = ((uint16_t)req[2] << 8U) | req[3];
    uint16_t count = ((uint16_t)req[4] << 8U) | req[5];

    if (count < 1U || count > MB_MAX_REGISTERS)
    {
        prv_SendException(req[0], req[1], MB_EX_ILLEGAL_DATA_VALUE);
        return -1;
    }

    s_resp_buf[0] = req[0]; /* slave addr */
    s_resp_buf[1] = req[1]; /* function code */
    s_resp_buf[2] = (uint8_t)(count * 2U); /* byte count */

    if (!prv_ReadRegisters(start, count, &s_resp_buf[3]))
    {
        prv_SendException(req[0], req[1], MB_EX_ILLEGAL_DATA_ADDRESS);
        return -1;
    }

    uint16_t resp_len = (uint16_t)(5U + count * 2U); /* addr+fc+bc+data+crc */
    MB_CRC16_Append(s_resp_buf, resp_len - MB_CHECKSUM_LEN);

    if (s_tx_cb) s_tx_cb(s_resp_buf, resp_len, s_tx_ctx);
    return (int32_t)resp_len;
}

/* =========================================================================
   FC 06: Write Single Register
   ========================================================================= */
static int32_t prv_FC06(const uint8_t *req, uint16_t req_len)
{
    if (req_len != 8U)
    {
        prv_SendException(req[0], req[1], MB_EX_ILLEGAL_DATA_VALUE);
        return -1;
    }

    uint16_t addr  = ((uint16_t)req[2] << 8U) | req[3];
    uint16_t value = ((uint16_t)req[4] << 8U) | req[5];

    /* Echo the request as the response (standard FC06 behaviour) */
    memcpy(s_resp_buf, req, 6U);
    MB_CRC16_Append(s_resp_buf, 6U);

    if (!MB_Slave_WriteRegister(addr, value))
    {
        prv_SendException(req[0], req[1], MB_EX_ILLEGAL_DATA_ADDRESS);
        return -1;
    }

    if (s_tx_cb) s_tx_cb(s_resp_buf, 8U, s_tx_ctx);
    return 8;
}

/* =========================================================================
   FC 16 (0x10): Write Multiple Registers
   ========================================================================= */
static int32_t prv_FC16(const uint8_t *req, uint16_t req_len)
{
    uint16_t start    = ((uint16_t)req[2] << 8U) | req[3];
    uint16_t count    = ((uint16_t)req[4] << 8U) | req[5];
    uint8_t  byte_cnt = req[6];

    if (count < 1U || count > 123U || byte_cnt != count * 2U ||
        req_len < (uint16_t)(7U + byte_cnt + 2U))
    {
        prv_SendException(req[0], req[1], MB_EX_ILLEGAL_DATA_VALUE);
        return -1;
    }

    if (!prv_WriteRegisters(start, count, &req[7]))
    {
        prv_SendException(req[0], req[1], MB_EX_ILLEGAL_DATA_ADDRESS);
        return -1;
    }

    /* Response: addr + fc + start_hi + start_lo + count_hi + count_lo + CRC */
    s_resp_buf[0] = req[0];
    s_resp_buf[1] = req[1];
    s_resp_buf[2] = req[2];
    s_resp_buf[3] = req[3];
    s_resp_buf[4] = req[4];
    s_resp_buf[5] = req[5];
    MB_CRC16_Append(s_resp_buf, 6U);

    if (s_tx_cb) s_tx_cb(s_resp_buf, 8U, s_tx_ctx);
    return 8;
}

/* =========================================================================
   Public API
   ========================================================================= */

void MB_Slave_Init(uint8_t slave_id,
                   const MB_Slave_RegBank_t *regs,
                   MB_Slave_TxCallback_t tx_cb,
                   void *tx_ctx)
{
    s_slave_id = slave_id;
    s_regs     = *regs;
    s_tx_cb    = tx_cb;
    s_tx_ctx   = tx_ctx;
}

int32_t MB_Slave_ProcessFrame(const uint8_t *frame, uint16_t len)
{
    if (len < MB_RTU_MIN_FRAME_LEN) return -1;

    /* Filter: not addressed to us (broadcast addr 0 is accepted only for writes) */
    if (frame[0] != s_slave_id && frame[0] != 0U) return 0;

    /* CRC check */
    if (!MB_CRC16_Verify(frame, len)) return -1;

    uint8_t fc = frame[1];

    switch (fc)
    {
    case MB_FC_READ_HOLDING_REGS:
    case MB_FC_READ_INPUT_REGS:
        if (frame[0] == 0U) return 0; /* broadcast forbidden for reads */
        return prv_FC03_04(frame, len);

    case MB_FC_WRITE_SINGLE_REG:
        return prv_FC06(frame, len);

    case MB_FC_WRITE_MULTIPLE_REGS:
        return prv_FC16(frame, len);

    default:
        prv_SendException(frame[0], fc, MB_EX_ILLEGAL_FUNCTION);
        return -1;
    }
}

bool MB_Slave_WriteRegister(uint16_t addr, uint16_t value)
{
    if (addr < s_regs.holding_start ||
        addr >= s_regs.holding_start + s_regs.holding_count)
        return false;

    s_regs.holding_regs[addr - s_regs.holding_start] = value;
    return true;
}

bool MB_Slave_ReadRegister(uint16_t addr, uint16_t *out_value)
{
    if (addr < s_regs.holding_start ||
        addr >= s_regs.holding_start + s_regs.holding_count)
        return false;

    *out_value = s_regs.holding_regs[addr - s_regs.holding_start];
    return true;
}

