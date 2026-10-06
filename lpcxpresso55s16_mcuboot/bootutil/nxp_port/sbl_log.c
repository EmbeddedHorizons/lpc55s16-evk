/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "sbl_log.h"
#include "fsl_device_registers.h"
#include "fsl_clock.h"
#include "fsl_reset.h"
#include "board.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define SBL_LOG_USART     USART0
#define SBL_LOG_FLEXCOMM  FLEXCOMM0
#define SBL_LOG_CLK_HZ    12000000U /* FRO 12 MHz attached to FLEXCOMM0 */
#define SBL_LOG_FC_USART  1U        /* FLEXCOMM PSELID: USART */

/*******************************************************************************
 * Code
 ******************************************************************************/

void sbl_log_init(void)
{
    USART_Type *uart = SBL_LOG_USART;
    uint32_t best_err = UINT32_MAX;
    uint32_t best_osr = 15U;
    uint32_t best_brg = 0U;

    CLOCK_AttachClk(BOARD_DEBUG_UART_CLK_ATTACH);
    CLOCK_EnableClock(kCLOCK_FlexComm0);
    RESET_PeripheralReset(kFC0_RST_SHIFT_RSTn);

    SBL_LOG_FLEXCOMM->PSELID = FLEXCOMM_PSELID_PERSEL(SBL_LOG_FC_USART);

    /* baud = clk / ((osr + 1) * (brg + 1)), pick the pair with the smallest error */
    for (uint32_t osr = 15U; osr >= 4U; osr--)
    {
        uint32_t div = (osr + 1U) * BOARD_DEBUG_UART_BAUDRATE;
        uint32_t brg = (SBL_LOG_CLK_HZ + div / 2U) / div;
        if (brg == 0U)
        {
            continue;
        }
        uint32_t baud = SBL_LOG_CLK_HZ / ((osr + 1U) * brg);
        uint32_t err  = (baud > BOARD_DEBUG_UART_BAUDRATE) ? (baud - BOARD_DEBUG_UART_BAUDRATE) :
                                                              (BOARD_DEBUG_UART_BAUDRATE - baud);
        if (err < best_err)
        {
            best_err = err;
            best_osr = osr;
            best_brg = brg - 1U;
        }
    }

    uart->FIFOCFG = USART_FIFOCFG_ENABLETX_MASK | USART_FIFOCFG_EMPTYTX_MASK;
    uart->OSR     = best_osr;
    uart->BRG     = best_brg;
    uart->CFG     = USART_CFG_DATALEN(1U) | USART_CFG_ENABLE_MASK; /* 8N1 */
}

void sbl_log_deinit(void)
{
    USART_Type *uart = SBL_LOG_USART;

    if ((uart->CFG & USART_CFG_ENABLE_MASK) == 0U)
    {
        return;
    }
    while ((uart->FIFOSTAT & USART_FIFOSTAT_TXEMPTY_MASK) == 0U)
    {
    }
    while ((uart->STAT & USART_STAT_TXIDLE_MASK) == 0U)
    {
    }
    uart->CFG     = 0U;
    uart->FIFOCFG = 0U;
}

static void sbl_log_putc(char c)
{
    USART_Type *uart = SBL_LOG_USART;

    while ((uart->FIFOSTAT & USART_FIFOSTAT_TXNOTFULL_MASK) == 0U)
    {
    }
    uart->FIFOWR = (uint8_t)c;
}

static void sbl_log_puts(const char *s)
{
    while (*s != '\0')
    {
        sbl_log_putc(*s++);
    }
}

static void sbl_log_number(uint32_t value, uint32_t base, bool upper, bool negative, uint32_t width, char pad)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[11];
    uint32_t len = 0U;

    do
    {
        buf[len++] = digits[value % base];
        value /= base;
    } while (value != 0U);

    if (negative)
    {
        if (pad == '0')
        {
            sbl_log_putc('-');
        }
        else
        {
            buf[len++] = '-';
        }
        width = (width > 0U) ? width - 1U : 0U;
    }
    while (width > len)
    {
        sbl_log_putc(pad);
        width--;
    }
    while (len > 0U)
    {
        sbl_log_putc(buf[--len]);
    }
}

void sbl_log_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    for (; *fmt != '\0'; fmt++)
    {
        if (*fmt != '%')
        {
            sbl_log_putc(*fmt);
            continue;
        }

        char pad       = ' ';
        uint32_t width = 0U;

        fmt++;
        if (*fmt == '0')
        {
            pad = '0';
            fmt++;
        }
        while ((*fmt >= '0') && (*fmt <= '9'))
        {
            width = (width * 10U) + (uint32_t)(*fmt - '0');
            fmt++;
        }
        while ((*fmt == 'l') || (*fmt == 'z') || (*fmt == 'h'))
        {
            fmt++; /* all integer arguments are 32-bit on this target */
        }

        switch (*fmt)
        {
            case 'c':
                sbl_log_putc((char)va_arg(ap, int));
                break;
            case 's':
            {
                const char *s = va_arg(ap, const char *);
                sbl_log_puts((s != NULL) ? s : "(null)");
                break;
            }
            case 'd':
            case 'i':
            {
                int32_t v = va_arg(ap, int32_t);
                sbl_log_number((v < 0) ? (uint32_t)(-v) : (uint32_t)v, 10U, false, v < 0, width, pad);
                break;
            }
            case 'u':
                sbl_log_number(va_arg(ap, uint32_t), 10U, false, false, width, pad);
                break;
            case 'x':
            case 'X':
                sbl_log_number(va_arg(ap, uint32_t), 16U, *fmt == 'X', false, width, pad);
                break;
            case 'p':
                sbl_log_puts("0x");
                sbl_log_number((uint32_t)va_arg(ap, void *), 16U, false, false, 8U, '0');
                break;
            case '%':
                sbl_log_putc('%');
                break;
            case '\0':
                fmt--; /* trailing '%': stop at the terminator */
                break;
            default:
                sbl_log_putc('%');
                sbl_log_putc(*fmt);
                break;
        }
    }
    va_end(ap);
}
