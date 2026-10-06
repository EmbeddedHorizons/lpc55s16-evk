/*
 * Copyright 2021 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _FLASH_PARTITIONING_H_
#define _FLASH_PARTITIONING_H_

#define BOOT_FLASH_BASE     0x00000000

#if defined(CONFIG_BOOT_CUSTOM_DEVICE_SETUP)
/* Layout setup from Kconfig */

#define BOOT_FLASH_ACT_APP              CONFIG_BOOT_FLASH_ACT_APP_ADDRESS
#define BOOT_FLASH_CAND_APP             CONFIG_BOOT_FLASH_CAND_APP_ADDRESS

#else
/* Default layout setup */

/* LPC55S16 (244kB flash): MCUboot 0x00000-0x07FFF, primary slot 0x08000-0x21FFF,
 * secondary slot 0x22000-0x3BFFF (104kB each), 0x3C000-0x3CFFF free, PFR from 0x3D000 */
#define BOOT_FLASH_ACT_APP  0x00008000
#define BOOT_FLASH_CAND_APP 0x00022000

#endif /* CONFIG_BOOT_CUSTOM_DEVICE_SETUP */

#endif
