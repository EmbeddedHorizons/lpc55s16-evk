/*
 * Copyright 2021 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SBL_CONFIG_H__
#define SBL_CONFIG_H__

#define SOC_LPC55S16_SERIES

/*******************************************************************/
/* Use default configuration if setup from Kconfig is not provided */
/*******************************************************************/
#ifndef CONFIG_BOOT_CUSTOM_DEVICE_SETUP


/* Flash device parameters */

/* LPC55S16: 244kB usable flash (0x0-0x3CFFF, PFR above)
 * 32kB mcuboot + 104kB AppImage + 104kB AppImageNew + 4kB free */
#define COMPONENT_FLASHIAP_SIZE 245760

/* CONFIG_MCUBOOT_MAX_IMG_SECTORS > (AppImageSize / SectorSize) = 0x1A000 / 512 = 208.
 * Must be strictly greater: flash_area_get_sectors() fails when count == max. */
#define CONFIG_MCUBOOT_MAX_IMG_SECTORS 216

/*
 * LPC55S16 with ECC Flash limits use of revert strategies (move/swap).
 * At least with current MCUBoot implementation.
 */
#define MCUBOOT_OVERWRITE_ONLY

#define CONFIG_BOOT_BOOTSTRAP

/* Crypto */

/*
 * Application is authenticated by the Boot ROM API (skboot_authenticate)
 * against the RKTH in CMPA - the same root of trust used by ROM for MCUboot.
 * MCUboot itself only checks the SHA-256 hash TLV, no MCUboot signing key.
 * See securebootloader.md. Comment out to use MCUboot ECDSA P-256 signatures.
 */
#define CONFIG_BOOT_ROM_AUTHENTICATION

#ifdef CONFIG_BOOT_ROM_AUTHENTICATION
/* Only the SHA-256 image hash is computed by MCUboot: small TinyCrypt SHA-256 */
#define CONFIG_BOOT_USE_TINYCRYPT
#else
#define CONFIG_BOOT_SIGNATURE
#define CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256
#define CONFIG_BOOT_USE_PSA_CRYPTO
#endif

#endif /* CONFIG_BOOT_CUSTOM_DEVICE_SETUP */

#endif
