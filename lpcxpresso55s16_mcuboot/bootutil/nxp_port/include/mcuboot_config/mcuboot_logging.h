/*
 * Copyright (c) 2018 Runtime Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __MCUBOOT_LOGGING_H__
#define __MCUBOOT_LOGGING_H__

#include "sbl_log.h"

#ifdef NDEBUG
#undef assert
#define assert(x) ((void)(x))
#endif

#define MCUBOOT_LOG_MODULE_DECLARE(domain)
#define MCUBOOT_LOG_MODULE_REGISTER(domain)

/* Tiny polled UART logger (sbl_log.c) instead of the SDK debug console */
#define MCUBOOT_LOG_ERR(...)             \
    {                                    \
        sbl_log_printf(__VA_ARGS__);     \
        sbl_log_printf("\r\n");          \
    }
#define MCUBOOT_LOG_WRN(...)             \
    {                                    \
        sbl_log_printf(__VA_ARGS__);     \
        sbl_log_printf("\r\n");          \
    }
#define MCUBOOT_LOG_INF(...)             \
    {                                    \
        sbl_log_printf(__VA_ARGS__);     \
        sbl_log_printf("\r\n");          \
    }
#define MCUBOOT_LOG_DBG(...)             \
    {                                    \
        sbl_log_printf(__VA_ARGS__);     \
        sbl_log_printf("\r\n");          \
    }

#endif /* __MCUBOOT_LOGGING_H__ */
