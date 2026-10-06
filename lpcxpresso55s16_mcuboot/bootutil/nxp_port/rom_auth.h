/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __ROM_AUTH_H__
#define __ROM_AUTH_H__

#include "bootutil/fault_injection_hardening.h"

/*
 * Application authentication by Boot ROM API (skboot_authenticate).
 *
 * The application in each slot is:  [MCUboot header][signed MBI][MCUboot TLVs]
 * The signed MBI is verified by ROM against the RKTH in CMPA, the same root of
 * trust the ROM uses to authenticate MCUboot itself.
 */

/* Returns 0 when the ROM authentication API is present on this silicon */
int rom_auth_init(void);

/* Authenticates the MBI of the given image slot, returns FIH_SUCCESS or FIH_FAILURE */
fih_ret rom_auth_check_slot(int img_index, int slot);

#endif /* __ROM_AUTH_H__ */
