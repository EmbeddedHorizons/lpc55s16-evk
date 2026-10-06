/*
 * Copyright (c) 2013 - 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2021 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_device_registers.h"
#include "sbl_log.h"
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "boot.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* Log the maximum stack use (MCUboot + ROM API call) before jumping to the application */
#ifndef SBL_STACK_WATERMARK
#define SBL_STACK_WATERMARK 1
#endif

#define SBL_STACK_PATTERN 0xA5A5A5A5U

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

#if SBL_STACK_WATERMARK
/* Stack limits from the MCUXpresso managed linker script */
extern uint32_t _vStackBase;
extern uint32_t _vStackTop;
#endif

/*******************************************************************************
 * Code
 ******************************************************************************/

#if SBL_STACK_WATERMARK
/* Fill the unused part of the stack (below the current SP) with a pattern */
static void sbl_stack_paint(void)
{
    uint32_t *p   = &_vStackBase;
    uint32_t *end = (uint32_t *)(__get_MSP() - 64U); /* keep clear of this frame */

    while (p < end)
    {
        *p++ = SBL_STACK_PATTERN;
    }
}

/* Deepest stack use since sbl_stack_paint() */
static void sbl_stack_report(void)
{
    const uint32_t *p = &_vStackBase;
    const uint32_t *top = &_vStackTop;

    while ((p < top) && (*p == SBL_STACK_PATTERN))
    {
        p++;
    }
    sbl_log_printf("Stack used: %u of %u bytes\r\n", (unsigned)((uint32_t)top - (uint32_t)p),
                   (unsigned)((uint32_t)top - (uint32_t)&_vStackBase));
}
#endif

/*!
 * @brief Main function
 */
int main(void)
{
#if SBL_STACK_WATERMARK
    sbl_stack_paint();
#endif

    BOARD_BootClockFROHF96M();
    BOARD_InitPins();
    sbl_log_init();

    sbl_log_printf("hello sbl.\r\n");

    (void)sbl_boot_main();

    return 0;
}

void SBL_DisablePeripherals(void)
{
#if SBL_STACK_WATERMARK
    sbl_stack_report();
#endif
    sbl_log_deinit();
}
