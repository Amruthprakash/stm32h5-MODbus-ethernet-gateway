/*
 * mb_master.c
 *
 *  Created on: May 11, 2026
 *      Author: LENOVO
 */


/**
 * @file    mb_master.c
 * @brief   Modbus RTU Master — HAL-decoupled, RTOS-aware
 *
 * Ported from STM32F4 Modbus.c. All HAL_UART_Transmit() calls removed.
 * CRC duplicates replaced with shared mb_crc16 module.
 * Parser state machine preserved from original Maslyakov implementation.
 */
#include <string.h>
#include "mb_master.h"
#include "mb_crc16.h"
#include "mb_frame.h"

/* =========================================================================
   Private state
   ========================================================================= */
static MB_MasterState_t    s_state;
static MB_MasterResponse_t s_resp;

/* Parser sub-states */
typedef enum {
    PARSE_SLAVE_ADDR = 0,
    PARSE_FUNC_CODE,
    PARSE_DATA,
} ParseState_t;

/* =========================================================================
   Private helpers
   ========================================================================= */

/** Build the first 6 bytes common to all standard queries */
static uint16_t prv_BuildBasis(uint8_t slave, uint8_t fc,
                               uint16_t start, uint16_t count,
                               uint8_t *buf)
{
    buf[0] = slave;
    buf[1] = fc;
    buf[2] = (uint8_t)(start >> 8);
    buf[3] = (uint8_t)(start & 0xFFU);
    buf[4] = (uint8_t)(count >> 8);
    buf[5] = (uint8_t)(count & 0xFFU);
    return 6U;
}

/** Compute how many bytes the slave should echo back */
static uint16_t prv_ComputeExpectedLen(const uint8_t *query)
{
    uint16_t nb;
    switch (query[1])
    {
    case MB_FC_READ_COILS:
    case MB_FC_READ_DISCRETE_INPUTS:
        nb = ((uint16_t)query[4] << 8U) | query[5];
        return (uint16_t)(3U + (nb / 8U) + ((nb % 8U) ? 1U : 0U)) + MB_CHECKSUM_LEN;

    case MB_FC_READ_HOLDING_REGS:
    case MB_FC_READ_INPUT_REGS:
        nb = ((uint16_t)query[4] << 8U) | query[5];
        return (uint16_t)(3U + 2U * nb) + MB_CHECKSUM_LEN;

    case MB_FC_WRITE_SINGLE_COIL:
    case MB_FC_WRITE_SINGLE_REG:
    case MB_FC_WRITE_MULTIPLE_COILS:
    case MB_FC_WRITE_MULTIPLE_REGS:
        return 6U + MB_CHECKSUM_LEN;

    default:
        return 0U; /* Unknown / user-defined: caller must handle timeout */
    }
}

/* =========================================================================
   Public API
   ========================================================================= */

void MB_Master_Init(uint8_t slave_id, uint8_t function_code)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(&s_resp,  0, sizeof(s_resp));
    s_state.slave_id      = slave_id;
    s_state.function_code = function_code;
}

int32_t MB_Master_BuildQuery(const MB_MasterQuery_t *query)
{
    if (!query) return MB_MASTER_ERR_PARAM;

    uint8_t  *buf = s_state.tx_buf;
    uint16_t  len = 0U;
    uint8_t   fc  = s_state.function_code;

    switch (fc)
    {
    /* --- Read functions -------------------------------------------------- */
    case MB_FC_READ_COILS:
    case MB_FC_READ_DISCRETE_INPUTS:
        if (query->count > MB_MAX_COILS) return MB_MASTER_ERR_LEN;
        len = prv_BuildBasis(s_state.slave_id, fc,
                             query->addr, query->count, buf);
        break;

    case MB_FC_READ_HOLDING_REGS:
    case MB_FC_READ_INPUT_REGS:
        if (query->count > MB_MAX_REGISTERS) return MB_MASTER_ERR_LEN;
        len = prv_BuildBasis(s_state.slave_id, fc,
                             query->addr, query->count, buf);
        break;

    /* --- Single-write functions ------------------------------------------ */
    case MB_FC_WRITE_SINGLE_COIL:
    {
        uint16_t coil_val = query->count ? 0xFF00U : 0x0000U;
        len = prv_BuildBasis(s_state.slave_id, fc,
                             query->addr, coil_val, buf);
        break;
    }
    case MB_FC_WRITE_SINGLE_REG:
        len = prv_BuildBasis(s_state.slave_id, fc,
                             query->addr, query->count, buf);
        break;

    /* --- Multi-write: FC 0x0F (coils) ------------------------------------ */
    case MB_FC_WRITE_MULTIPLE_COILS:
    {
        if (query->count > MB_MAX_COILS || !query->data)
            return MB_MASTER_ERR_PARAM;
        len = prv_BuildBasis(s_state.slave_id, fc,
                             query->addr, query->count, buf);
        uint16_t byte_cnt = (uint16_t)((query->count / 8U) + ((query->count % 8U) ? 1U : 0U));
        buf[len++] = (uint8_t)byte_cnt;
        /* Pack bit array */
        uint16_t check = 0, pos = 0;
        for (uint16_t i = 0; i < byte_cnt; i++)
        {
            int32_t bit = 0x01;
            buf[len] = 0;
            while ((bit & 0xFF) && (check++ < query->count))
            {
                if (query->data[pos++]) buf[len] |= (uint8_t)bit;
                bit <<= 1;
            }
            len++;
        }
        break;
    }

    /* --- Multi-write: FC 0x10 (holding registers) ------------------------ */
    case MB_FC_WRITE_MULTIPLE_REGS:
    {
        if (query->count > MB_MAX_REGISTERS || !query->data)
            return MB_MASTER_ERR_PARAM;
        len = prv_BuildBasis(s_state.slave_id, fc,
                             query->addr, query->count, buf);
        buf[len++] = (uint8_t)(query->count * 2U);
        for (uint16_t i = 0; i < query->count * 2U; i++)
            buf[len++] = query->data[i];
        break;
    }

    default:
        /* User-defined function — raw payload in data[], count = byte count */
        if (query->count > MB_RTU_MAX_FRAME_LEN - 4U || !query->data)
            return MB_MASTER_ERR_PARAM;
        buf[0] = s_state.slave_id;
        buf[1] = fc;
        memcpy(&buf[2], query->data, query->count);
        len = (uint16_t)(2U + query->count);
        break;
    }

    MB_CRC16_Append(buf, len);
    len += MB_CHECKSUM_LEN;
    s_state.tx_len          = len;
    s_state.expected_rx_len = prv_ComputeExpectedLen(buf);

    /* Reset RX side */
    memset(&s_resp, 0, sizeof(s_resp));

    return (int32_t)s_state.expected_rx_len;
}

bool MB_Master_GetTxFrame(const uint8_t **out_buf, uint16_t *out_len)
{
    if (s_state.tx_len == 0U) return false;
    *out_buf = s_state.tx_buf;
    *out_len = s_state.tx_len;
    return true;
}

MB_MasterResult_t MB_Master_ReceiveByte(uint8_t byte)
{
    if (s_resp.rx_len >= MB_RTU_MAX_FRAME_LEN)
        return MB_MASTER_ERR_LEN;

    /* First byte resets state machine */
    if (s_resp.rx_len == 0U)
        s_resp.parse_state = PARSE_SLAVE_ADDR;

    s_resp.buf[s_resp.rx_len++] = byte;

    switch ((ParseState_t)s_resp.parse_state)
    {
    case PARSE_SLAVE_ADDR:
        s_resp.parse_state = PARSE_FUNC_CODE;
        if (byte != s_state.slave_id)
            return MB_MASTER_ERR_SLAVE;
        return MB_MASTER_RX_ONGOING;

    case PARSE_FUNC_CODE:
        s_resp.parse_state = PARSE_DATA;
        if (byte == (s_state.function_code | MB_FC_EXCEPTION_MASK))
        {
            /* Exception response is always 5 bytes */
            s_state.expected_rx_len = 5U;
        }
        else if (byte != s_state.function_code)
        {
            return MB_MASTER_ERR_FUNC;
        }
        return MB_MASTER_RX_ONGOING;

    case PARSE_DATA:
        if (s_state.expected_rx_len == 0U)
        {
            /* Unknown length — rely on 3.5-char timeout externally */
            return MB_MASTER_RX_ONGOING;
        }
        if (s_resp.rx_len < s_state.expected_rx_len)
            return MB_MASTER_RX_ONGOING;

        /* Full frame received — check CRC */
        if (!MB_CRC16_Verify(s_resp.buf, s_resp.rx_len))
            return MB_MASTER_ERR_CRC;

        return MB_MASTER_RX_COMPLETE;

    default:
        return MB_MASTER_ERR_DATA;
    }
}

MB_MasterResult_t MB_Master_ProcessResponse(void)
{
    if (s_resp.rx_len < MB_RTU_MIN_FRAME_LEN)
        return MB_MASTER_ERR_TIMEOUT;

    /* Check for Modbus exception */
    if (s_resp.buf[1] & MB_FC_EXCEPTION_MASK)
    {
        s_resp.exception_code = s_resp.buf[2];
        return MB_MASTER_ERR_EXCEPTION;
    }

    /* Byte-count consistency for register reads */
    uint8_t fc = s_resp.buf[1];
    if (fc == MB_FC_READ_HOLDING_REGS ||
        fc == MB_FC_READ_INPUT_REGS   ||
        fc == MB_FC_READ_COILS        ||
        fc == MB_FC_READ_DISCRETE_INPUTS)
    {
        if ((uint16_t)(s_resp.rx_len - 5U) != (uint16_t)s_resp.buf[2])
            return MB_MASTER_ERR_DATA;
    }

    return MB_MASTER_OK;
}

void MB_Master_GetResponseData(const uint8_t **out_data, uint16_t *out_len)
{
    /* Payload starts after slave_addr + fc + byte_count; ends before CRC */
    if (s_resp.rx_len > 5U)
    {
        *out_data = &s_resp.buf[3];
        *out_len  = (uint16_t)(s_resp.rx_len - 5U);
    }
    else
    {
        *out_data = NULL;
        *out_len  = 0U;
    }
}

void MB_Master_Reset(void)
{
    memset(&s_resp, 0, sizeof(s_resp));
    s_state.tx_len = 0U;
}
