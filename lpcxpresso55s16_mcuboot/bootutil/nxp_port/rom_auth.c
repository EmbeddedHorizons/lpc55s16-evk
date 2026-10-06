/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sblconfig.h"

#ifdef CONFIG_BOOT_ROM_AUTHENTICATION

#include <stdbool.h>
#include <stdint.h>

#include "rom_auth.h"
#include "fsl_common.h"
#include "fsl_clock.h"
#include "fsl_iap_skboot_authenticate.h"
#include "mflash_drv.h"
#include "flash_map.h"
#include "sysflash/sysflash.h"
#include "bootutil/image.h"
#include "bootutil/bootutil_log.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* LPC55S16 ROM API tree (BOOTLOADER_API_TREE_POINTER for LPC55S16_SERIES in fsl_iap.c),
 * layout must match bootloader_tree_t in fsl_iap.c. LPC55S69: 0x130010f0 */
#define ROM_API_TREE_ADDR 0x1301fe00U
#define ROM_START         0x13000000U
#define ROM_END           0x13020000U

/* MBI header fields (offsets in the vector table area) */
#define MBI_IMAGE_LENGTH_OFFSET 0x20U
#define MBI_IMAGE_TYPE_OFFSET   0x24U
#define MBI_CERT_OFFSET_OFFSET  0x28U

/* Image type [5:0]: 0 = plain image, others are signed/CRC variants */
#define MBI_IMAGE_TYPE_MASK  0x3FU
#define MBI_IMAGE_TYPE_PLAIN 0x0U

typedef struct
{
    skboot_status_t (*skboot_authenticate_function)(const uint8_t *imageStartAddr, secure_bool_t *isSignVerified);
    void (*skboot_hashcrypt_irq_handler)(void);
} rom_skboot_interface_t;

typedef struct
{
    void (*runBootloader)(void *arg);
    uint32_t bootloader_version;
    const char *copyright;
    const uint32_t reserved0;
    const void *flashDriver;
    const void *kbApi;
    const uint32_t reserved1[4];
    const rom_skboot_interface_t *skbootAuthenticate;
} rom_api_tree_t;

#define ROM_API_TREE ((const rom_api_tree_t *)ROM_API_TREE_ADDR)

/* ROM reserved working RAM (non-secure alias), reported by blhost get-property 12.
 * LPC55S16: 0x2000_0000-0x2000_7FFF (LPC55S69: 0x2000_0000-0x2000_5FFF) */
#define ROM_RESERVED_RAM_START 0x20000000U
#define ROM_RESERVED_RAM_END   0x20008000U

/* Start of MCUboot .data (MCUXpresso managed linker script), .bss and heap follow it */
extern uint32_t _data;

/*******************************************************************************
 * Variables
 ******************************************************************************/

static bool s_rom_api_ok = false;

/* Set while the ROM owns HASHCRYPT, so its interrupt is routed to the ROM handler */
static volatile bool s_rom_auth_active = false;

/*******************************************************************************
 * Code
 ******************************************************************************/

static bool is_rom_addr(const void *p)
{
    uint32_t addr = (uint32_t)p;
    return (addr >= ROM_START) && (addr < ROM_END);
}

/* Overrides the WEAK handler from startup, so the HASHCRYPT interrupt
 * reaches the ROM handler while the ROM is using the peripheral. */
void HASHCRYPT_IRQHandler(void)
{
    if (s_rom_auth_active)
    {
        ROM_API_TREE->skbootAuthenticate->skboot_hashcrypt_irq_handler();
    }
    else
    {
        /* Nothing else in MCUboot uses HASHCRYPT interrupts (SHA-256 is TinyCrypt) */
        (void)DisableIRQ(HASHCRYPT_IRQn);
    }
    SDK_ISR_EXIT_BARRIER;
}

int rom_auth_init(void)
{
    const rom_api_tree_t *tree = ROM_API_TREE;
    const rom_skboot_interface_t *skboot = tree->skbootAuthenticate;

    /* The ROM uses 0x2000_0000-0x2000_7FFF (secure alias 0x3000_0000-) as working RAM
     * (blhost get-property 12). MCUboot .data/.bss/heap must start above it (linker SRAM region). */
    uint32_t ram_start = (uint32_t)&_data & ~0x10000000U;
    if ((ram_start >= ROM_RESERVED_RAM_START) && (ram_start < ROM_RESERVED_RAM_END))
    {
        BOOT_LOG_ERR("MCUboot RAM @0x%x overlaps ROM reserved RAM 0x%x-0x%x, fix the SRAM memory region",
                     (unsigned)&_data, (unsigned)ROM_RESERVED_RAM_START, (unsigned)(ROM_RESERVED_RAM_END - 1U));
        s_rom_api_ok = false;
        return -1;
    }

    BOOT_LOG_INF("ROM API version %u.%u.%u", (unsigned)((tree->bootloader_version >> 16) & 0xFFU),
                 (unsigned)((tree->bootloader_version >> 8) & 0xFFU), (unsigned)(tree->bootloader_version & 0xFFU));

    if (!is_rom_addr(skboot) || !is_rom_addr((const void *)skboot->skboot_authenticate_function) ||
        !is_rom_addr((const void *)skboot->skboot_hashcrypt_irq_handler))
    {
        BOOT_LOG_ERR("ROM authentication API not available (skbootAuthenticate @0x%08x)", (unsigned)skboot);
        s_rom_api_ok = false;
        return -1;
    }

    BOOT_LOG_INF("ROM authentication API @0x%08x", (unsigned)skboot->skboot_authenticate_function);
    s_rom_api_ok = true;
    return 0;
}

/* The ROM reads the whole MBI through the AHB bus. On LPC55S16/S69 an erased or
 * ECC-corrupted page causes a bus fault, so make sure every page is readable. */
static bool mbi_region_readable(uint32_t addr, uint32_t len)
{
    uint32_t page = addr - (addr % MFLASH_PAGE_SIZE);
    uint32_t end  = addr + len;

    for (; page < end; page += MFLASH_PAGE_SIZE)
    {
        if (mflash_drv_is_readable(page) != kStatus_Success)
        {
            return false;
        }
    }
    return true;
}

fih_ret rom_auth_check_slot(int img_index, int slot)
{
    const struct flash_area *fap = NULL;
    struct image_header hdr;
    uint32_t mbi_hdr[(MBI_CERT_OFFSET_OFFSET / 4U) + 1U];
    uint32_t mbi_addr;
    uint32_t mbi_len;
    skboot_status_t status   = kStatus_SKBOOT_Fail;
    secure_bool_t   verified = kSECURE_FALSE;
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    if (!s_rom_api_ok)
    {
        BOOT_LOG_ERR("ROM auth: API not available");
        FIH_RET(FIH_FAILURE);
    }

    BOOT_LOG_INF("ROM auth: slot %d check start", slot);

    if (flash_area_open(flash_area_id_from_multi_image_slot(img_index, slot), &fap) != 0)
    {
        BOOT_LOG_ERR("ROM auth: slot %d flash area open failed", slot);
        FIH_RET(FIH_FAILURE);
    }

    /* MCUboot header was already validated by the caller, read it again for our own checks */
    if (flash_area_read(fap, 0, &hdr, sizeof(hdr)) != 0 || hdr.ih_magic != IMAGE_MAGIC)
    {
        BOOT_LOG_ERR("ROM auth: slot %d MCUboot header not readable", slot);
        goto out;
    }

    /* The MBI starts right after the MCUboot header */
    if (flash_area_read(fap, hdr.ih_hdr_size, mbi_hdr, sizeof(mbi_hdr)) != 0)
    {
        BOOT_LOG_ERR("ROM auth: slot %d MBI header not readable", slot);
        goto out;
    }

    mbi_addr = fap->fa_off + hdr.ih_hdr_size;
    mbi_len  = mbi_hdr[MBI_IMAGE_LENGTH_OFFSET / 4U];

    /* Reject a plain (unsigned) MBI early, and an MBI that doesn't fit the MCUboot image */
    if ((mbi_hdr[MBI_IMAGE_TYPE_OFFSET / 4U] & MBI_IMAGE_TYPE_MASK) == MBI_IMAGE_TYPE_PLAIN)
    {
        BOOT_LOG_ERR("ROM auth: slot %d holds an unsigned MBI", slot);
        goto out;
    }
    if (mbi_len == 0U || mbi_len > hdr.ih_img_size)
    {
        BOOT_LOG_ERR("ROM auth: slot %d MBI length 0x%x invalid (image size 0x%x)", slot, (unsigned)mbi_len,
                     (unsigned)hdr.ih_img_size);
        goto out;
    }
    if (!mbi_region_readable(mbi_addr, mbi_len))
    {
        BOOT_LOG_ERR("ROM auth: slot %d MBI region not fully programmed", slot);
        goto out;
    }

    CLOCK_EnableClock(kCLOCK_HashCrypt);
    CLOCK_EnableClock(kCLOCK_Casper);

    BOOT_LOG_INF("ROM auth: slot %d calling ROM (MBI @0x%x, len 0x%x)", slot, (unsigned)mbi_addr, (unsigned)mbi_len);

    /* The ROM may wait for the HASHCRYPT interrupt, which is routed to it by HASHCRYPT_IRQHandler */
    bool irq_was_enabled = (NVIC_GetEnableIRQ(HASHCRYPT_IRQn) != 0U);
    NVIC_ClearPendingIRQ(HASHCRYPT_IRQn);
    s_rom_auth_active = true;
    (void)EnableIRQ(HASHCRYPT_IRQn);

    status = ROM_API_TREE->skbootAuthenticate->skboot_authenticate_function((const uint8_t *)mbi_addr, &verified);

    if (!irq_was_enabled)
    {
        (void)DisableIRQ(HASHCRYPT_IRQn);
    }
    s_rom_auth_active = false;

    BOOT_LOG_INF("ROM auth: slot %d ROM returned status 0x%08x verified 0x%08x", slot, (unsigned)status,
                 (unsigned)verified);

    /* Both outputs are multi-bit constants, check both of them twice to make glitching harder */
    if ((status == kStatus_SKBOOT_Success) && (verified == kSECURE_TRACKER_VERIFIED))
    {
        if ((verified == kSECURE_TRACKER_VERIFIED) && (status == kStatus_SKBOOT_Success))
        {
            BOOT_LOG_INF("ROM auth: slot %d MBI @0x%x verified", slot, (unsigned)mbi_addr);
            FIH_SET(fih_rc, FIH_SUCCESS);
            goto out;
        }
    }

    BOOT_LOG_ERR("ROM auth: slot %d failed, status 0x%08x verified 0x%08x", slot, (unsigned)status,
                 (unsigned)verified);

out:
    flash_area_close(fap);
    FIH_RET(fih_rc);
}

#endif /* CONFIG_BOOT_ROM_AUTHENTICATION */
