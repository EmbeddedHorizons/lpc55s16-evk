/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SBL_LOG_H__
#define __SBL_LOG_H__

/*
 * Minimal polled UART logger for MCUboot (replaces the SDK debug console).
 * Uses the board debug UART (FLEXCOMM0 / USART0, 12 MHz FRO, 115200 8N1).
 * Supported formats: %c %s %d %i %u %x %X %p %%, flag '0', width, length 'l'/'z' (ignored).
 */

void sbl_log_init(void);

/* Waits until all characters are sent, then disables the UART */
void sbl_log_deinit(void);

void sbl_log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* __SBL_LOG_H__ */
