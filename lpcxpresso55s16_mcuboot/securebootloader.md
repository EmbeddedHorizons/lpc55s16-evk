# Secure Boot Chain with a Single Root of Trust (LPC55S16 + MCUboot)

## 0. LPC55S16 port (read this first)

This project was copied from the LPCXpresso55S69 MCUboot project. Sections 1-13 below describe the
LPC55S69 work and its hardware results. **On the LPC55S16 the following values replace the LPC55S69 ones:**

| Item | LPC55S69 (sections 1-13) | **LPC55S16 (this project)** |
|---|---|---|
| SDK files (device, startup, drivers, board) | SDK 25.9.0 LPCXpresso55S69 | SDK 25.9.0 LPCXpresso55S16 (`device/periph2`, `startup_lpc55s16.c`, board from `hello_world`) |
| Defines | `CPU_LPC55S69JBD100_cm33_core0`, `LPC55S69_cm33_core0_SERIES` | `CPU_LPC55S16JBD100`, `CPU_LPC55S16JBD100_cm33`, `LPC55S16_SERIES` |
| Usable flash | 630 KB | 244 KB (`0x00000`-`0x3CFFF`, PFR from `0x3D000`) |
| MCUboot | `0x00000`-`0x07FFF` | `0x00000`-`0x07FFF` (32 KB, Release 12,064 B) |
| Primary slot | `0x20000`-`0x4FFFF` (192 KB) | **`0x08000`-`0x21FFF` (104 KB)** |
| Secondary slot | `0x50000`-`0x7FFFF` (192 KB) | **`0x22000`-`0x3BFFF` (104 KB)** |
| Free | - | `0x3C000`-`0x3CFFF` (4 KB) |
| App link address / size | `0x20400` / `0x2FC00` | **`0x08400` / `0x19C00`** |
| `imgtool --slot-size` / `--max-sectors` | `0x30000` / `400` | **`0x1A000` / `216` (must be > 208 sectors, see `sblconfig.h`)** |
| MBI `outputImageExecutionAddress` (app) | `0x00020400` | **`0x00008400`** |
| ROM API tree (`rom_auth.c`) | `0x130010f0` | **`0x1301fe00`** (`fsl_iap.c`, `LPC55S16_SERIES`) |
| ROM reserved RAM (`blhost get-property 12`) | `0x20000000`-`0x20005FFF` (24 KB) | **`0x20000000`-`0x20007FFF` (32 KB)**, SRAMX `0x04000000`-`0x04003FFF`, `0x14000000`-`0x14001FFF` (measured 2026-10-03) |
| MCUboot SRAM | `0x20006000`, 16 KB | **`0x20008000`, 16 KB** (stack top `0x2000C000`) |
| SRAMX / USB_RAM | 32 KB / `0x20040000` | 16 KB / `0x20010000` |

Board test (2026-10-03): signed MCUboot at `0x0` + signed `freertos_hello` (linked at `0x8400`) in the primary
slot. ROM returns `0x5ac3c35a` / `0x55aacc33`, MCUboot jumps to `0x8400`, the app prints `Hello world.`
MCUboot stack use 1,696 of 4,096 B.

App flow on the S16 (secure_provisioning workspace in `..\secure_provisioning`, ISP on COM3, 57600):

```bat
arm-none-eabi-objcopy -O binary lpcxpresso55s16_freertos_hello.axf ..\source_images\freertos_hello_app.bin
cd ..\secure_provisioning\configs
nxpimage mbi export -c freertos_hello_mbi_config.yaml
cd ..\bootable_images
imgtool sign --header-size 0x400 --pad-header --align 4 --version 1.0.0 ^
  --slot-size 0x1A000 --max-sectors 216 freertos_hello_app_mbi.bin freertos_hello_app_signed.bin
blhost -p COM3,57600 flash-erase-region 0x8000 0x1A000
blhost -p COM3,57600 write-memory 0x8000 freertos_hello_app_signed.bin
```

Build from the command line: `.\scripts\build.ps1 [-Config Release] [-Flash]` (same scripts as `lpcxpresso55s16_freertos_hello`).


## 1. Goal

Use **one root key (one RKTH in CMPA)** to authenticate the whole boot chain:

```
 Reset
   │
   ▼
 Boot ROM ── authenticates ──► MCUboot (SBL)  @ 0x00000000
   │   (RSA-2048 cert chain vs. RKTH in CMPA)
   │
   ▼
 MCUboot ── calls ROM API skboot_authenticate() ──► Application  @ 0x00020400
       (same cert chain, same RKTH, same signing tool)
```

- One algorithm (RSA-2048 with SHA-256), one certificate chain (ROTx → IMGx_1), and one signing flow (Secure Provisioning Tool / `nxpimage`).
- Key revocation and firmware-version (anti-rollback) policy set in CMPA/CFPA covers **both** stages.
- MCUboot stores **no** public key. The trust anchor lives only in the protected flash area (CMPA).

> **Note:** LPC55S69 has no fuse-based OTP for the key. The RKTH is stored in **CMPA** (Protected Flash Region). CMPA acts like OTP once it is **sealed**. Sealing is irreversible.

## 2. Why MCUboot's native check is not used

| | MCUboot native (`imgtool` signature) | ROM API `skboot_authenticate()` |
|---|---|---|
| Trust anchor | Public key compiled into MCUboot | RKTH in CMPA |
| Understands NXP certificate block / RKTH | No | Yes |
| Follows key revocation in CFPA | No | Yes |
| Implementation | Software (mbedTLS/PSA) | ROM, hardened |

The RKTH is `SHA256(H(ROT1) ‖ H(ROT2) ‖ H(ROT3) ‖ H(ROT4))`. It is not the hash of a single key, so MCUboot's `MCUBOOT_HW_KEY` cannot match it. The ROM API does the full certificate-chain check.

## 3. Flash layout

| Region | Address | Size | Content |
|---|---|---|---|
| MCUboot | `0x00000` – `0x07FFF` | 32 KB | MCUboot as a **signed MBI** (checked by ROM at reset). Linker region since 13.7 |
| (unused) | `0x08000` – `0x1FFFF` | 96 KB | Free. Kept so the slot addresses stay unchanged (13.7) |
| Primary slot | `0x20000` – `0x4FFFF` | 192 KB | MCUboot header (0x400) + app **signed MBI** + trailer |
| Secondary slot | `0x50000` – `0x7FFFF` | 192 KB | Update candidate (same format) |

The application is **linked to execute at `0x20400`**, right after the 0x400-byte MCUboot header.

### 3.1 RAM layout (MCUboot)

| Region | Address | Size | Used by |
|---|---|---|---|
| ROM reserved | `0x2000_0000` – `0x2000_5FFF` | 24 KB | **Boot ROM working RAM.** Reported by the ROM (`blhost get-property 12`). MCUboot must not use it (see 9.6 and 12) |
| MCUboot SRAM | `0x2000_6000` – `0x2000_9FFF` | 16 KB | `.data` / `.bss` 8.4 KB from `0x2000_6000`, no heap, stack 4 KB at the top (`0x2000_9000`–`0x2000_9FFF`, initial SP `0x2000_A000`). Measured stack use 1,712 B (13.7) |
| ROM reserved (SRAMX) | `0x0400_0000` – `0x0400_7FFF` | 32 KB | Boot ROM. MCUboot doesn't place anything in SRAMX |

Set in the MCUXpresso project memory configuration (`.cproject`: `SRAM` location `0x20006000`, size `0x4000`; heap `0x0`, stack `0x1000`). The IDE generates `Debug/*_Debug_memory.ld` from it.

## 4. Image format of the application

```
0x20000  ┌──────────────────────────────┐
         │ MCUboot header (0x400)       │  ← imgtool (no key)
0x20400  ├──────────────────────────────┤
         │ Application (vector table…)  │  ┐
         │ NXP certificate block        │  ├ signed MBI (Secure Provisioning Tool / nxpimage)
         │ RSA signature                │  ┘
         ├──────────────────────────────┤
         │ MCUboot TLVs (SHA-256 hash)  │  ← imgtool
         │ MCUboot trailer              │
         └──────────────────────────────┘
```

- **Inner layer (security):** NXP MBI signed with the same RSA certificate chain that the ROM uses for MCUboot.
- **Outer layer (slot management):** MCUboot header, TLVs and trailer. These give an integrity hash and the update/bootstrap logic. It carries no signature.

## 5. Implementation in MCUboot

Everything in this section is **implemented** in this project, behind one switch: `CONFIG_BOOT_ROM_AUTHENTICATION`. The code excerpts below are copied from the source files.

### 5.1 Switch: `source/sblconfig.h`

```c
/* Crypto */

/*
 * Application is authenticated by the Boot ROM API (skboot_authenticate)
 * against the RKTH in CMPA - the same root of trust used by ROM for MCUboot.
 * MCUboot itself only checks the SHA-256 hash TLV, no MCUboot signing key.
 * See securebootloader.md. Comment out to use MCUboot ECDSA P-256 signatures.
 */
#define CONFIG_BOOT_ROM_AUTHENTICATION

#ifndef CONFIG_BOOT_ROM_AUTHENTICATION
#define CONFIG_BOOT_SIGNATURE
#define CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256
#endif

/* Still needed for the SHA-256 image hash */
#define CONFIG_BOOT_USE_PSA_CRYPTO
```

- Switch **on** (default): ROM authentication, no MCUboot signature, no key in MCUboot.
- Switch **off**: the original SDK behaviour (MCUboot ECDSA P-256 with the key in `keys/`).

### 5.2 Configuration: `bootutil/nxp_port/include/mcuboot_config/mcuboot_config.h`

```c
#ifdef CONFIG_BOOT_ROM_AUTHENTICATION
#if defined(CONFIG_BOOT_SIGNATURE) || defined(CONFIG_BOOT_ENCRYPT_RSA) || defined(CONFIG_BOOT_ENCRYPT_EC256)
#error "CONFIG_BOOT_ROM_AUTHENTICATION cannot be combined with MCUboot signatures or encrypted images"
#endif
#if defined(CONFIG_ENCRYPT_XIP_EXT_ENABLE) || defined(CONFIG_MCUBOOT_FLASH_REMAP_ENABLE)
#error "CONFIG_BOOT_ROM_AUTHENTICATION is only supported with plain internal flash images"
#endif
#ifndef MCUBOOT_IMAGE_ACCESS_HOOKS
#define MCUBOOT_IMAGE_ACCESS_HOOKS
#endif
#define MCUBOOT_VALIDATE_PRIMARY_SLOT
#endif
```

| Define | Why |
|---|---|
| `MCUBOOT_IMAGE_ACCESS_HOOKS` | Without it, `boot_image_check_hook()` is never called (`bootutil/include/bootutil/boot_hooks.h:55`) |
| `MCUBOOT_VALIDATE_PRIMARY_SLOT` | Normally tied to `CONFIG_BOOT_SIGNATURE`, which is off. Without it, the ROM check would run only when an update is installed, not at every boot |
| `#error` checks | Stop unsupported combinations at build time instead of producing an insecure image |

### 5.3 ROM authentication module: `bootutil/nxp_port/rom_auth.c` / `rom_auth.h` (new)

API (`rom_auth.h`):

```c
int     rom_auth_init(void);                          /* 0 = ROM authentication API present   */
fih_ret rom_auth_check_slot(int img_index, int slot); /* FIH_SUCCESS or FIH_FAILURE           */
```

**ROM API access.** `rom_auth.c` has its own copy of the ROM API tree layout (`rom_api_tree_t`, same layout as `bootloader_tree_t` in `fsl_iap.c`, see 9.2). It calls the ROM function pointers **directly**, so it doesn't depend on the private struct in `fsl_iap.c`:

```c
#define ROM_API_TREE_ADDR 0x130010f0U   /* LPC55S69 */
#define ROM_START         0x13000000U
#define ROM_END           0x13020000U
#define ROM_API_TREE ((const rom_api_tree_t *)ROM_API_TREE_ADDR)
```

**`rom_auth_init()`**, called once from `sbl_boot_main()`:
1. Logs the ROM API version: `ROM API version x.y.z`.
2. Checks that `skbootAuthenticate`, `skboot_authenticate_function` and `skboot_hashcrypt_irq_handler` all point **inside ROM** (`0x1300_0000`–`0x1301_FFFF`).
3. Logs `ROM authentication API @0x...` and returns 0. On failure it logs `ROM authentication API not available` and returns -1.

**`rom_auth_check_slot()`**, called from the hook for every slot MCUboot validates:

| Step | Check / action | Log |
|---|---|---|
| 1 | `rom_auth_init()` succeeded | on failure: `ROM auth: API not available` |
| 2 | Log start, open the slot flash area | `ROM auth: slot N check start`. On failure: `ROM auth: slot N flash area open failed` |
| 3 | Read the MCUboot header with `flash_area_read()`. Magic must be `IMAGE_MAGIC` | on failure: `ROM auth: slot N MCUboot header not readable` |
| 4 | Read the MBI header at `slot + ih_hdr_size` | on failure: `ROM auth: slot N MBI header not readable` |
| 5 | MBI image type `[5:0]` (`+0x24`) is **not** 0 (plain / unsigned) | on failure: `ROM auth: slot N holds an unsigned MBI` |
| 6 | MBI length (`+0x20`) is non-zero and ≤ `ih_img_size` | on failure: `ROM auth: slot N MBI length ... invalid` |
| 7 | Every 512-byte flash page of the MBI is readable (`mflash_drv_is_readable`). An erased or ECC-bad page would make the ROM bus-fault | on failure: `ROM auth: slot N MBI region not fully programmed` |
| 8 | Enable the **HASHCRYPT and CASPER** clocks | `ROM auth: slot N calling ROM (MBI @0x..., len 0x...)` |
| 9 | Clear pending HASHCRYPT IRQ, set `s_rom_auth_active`, **enable `HASHCRYPT_IRQn` in the NVIC**, call ROM `skboot_authenticate_function(mbi_addr, &verified)`, then restore the NVIC state and clear the flag | `ROM auth: slot N ROM returned status 0x... verified 0x...` (always) |
| 10 | `status == kStatus_SKBOOT_Success` **and** `verified == kSECURE_TRACKER_VERIFIED`, checked twice in swapped order | on failure: `ROM auth: slot N failed, status 0x... verified 0x...` |
| 11 | Success: return `FIH_SUCCESS` | `ROM auth: slot N MBI @0x... verified` |

The MBI address passed to the ROM is `fa_off + ih_hdr_size`, e.g. `0x0002_0400` (primary) or `0x0005_0400` (secondary).

ROM call (steps 8–9), as implemented:

```c
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
```

**HASHCRYPT interrupt routing** (fix for 9.4). A strong handler overrides the WEAK one in `startup_lpc55s69_cm33_core0.c`:

```c
void HASHCRYPT_IRQHandler(void)
{
    if (s_rom_auth_active)
    {
        ROM_API_TREE->skbootAuthenticate->skboot_hashcrypt_irq_handler();   /* ROM handler */
    }
    else
    {
        extern void HASHCRYPT_DriverIRQHandler(void);
        HASHCRYPT_DriverIRQHandler();                                     /* SDK fsl_hashcrypt.c */
    }
    SDK_ISR_EXIT_BARRIER;
}
```

### 5.4 Hook: `bootutil/nxp_port/bootutil_hooks.c`

```c
fih_int boot_image_check_hook(int img_index, int slot)
{
#ifdef CONFIG_BOOT_ROM_AUTHENTICATION
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    /* Signature check by Boot ROM against RKTH in CMPA */
    FIH_CALL(rom_auth_check_slot, fih_rc, img_index, slot);
    if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS))
    {
        FIH_RET(FIH_FAILURE);
    }

    /* ROM check passed -> MCUboot continues with its regular hash/TLV check */
    FIH_RET(FIH_BOOT_HOOK_REGULAR);
#else
    return BOOT_HOOK_REGULAR;
#endif
}
```

Return value logic (from `bootutil/src/loader.c:1095-1099`):

| Hook returns | Result |
|---|---|
| `FIH_BOOT_HOOK_REGULAR` | MCUboot also runs its own hash/TLV check → **both** checks must pass (used) |
| `FIH_SUCCESS` | MCUboot skips its own check → only the ROM check (not used) |
| `FIH_FAILURE` | Image rejected. A secondary-slot image is also erased by MCUboot |

The hook runs only after MCUboot has found a valid header (`boot_is_header_valid()`), so an empty slot never reaches the ROM call.

### 5.5 Other source changes

| File | Change |
|---|---|
| `bootutil/nxp_port/boot.c` | Includes `rom_auth.h`. In `sbl_boot_main()`, after the `Bootloader Version` log and before `boot_go()`: calls `rom_auth_init()` and **halts** (`FAILED to find ROM authentication API, halting`) if it fails (fail closed) |
| `bootutil/nxp_port/keys.c` | The `#error "No public key..."` is skipped when the switch is on, and `bootutil_keys[]` / `bootutil_key_cnt` are only compiled when an MCUboot signature type is set. With the switch on, **no public key is in the binary** |
| `.cproject` (memory configuration) | `SRAM` moved from `0x20000000` / `0x40000` to **`0x20006000` / `0x3a000`** (later reduced to `0x4000`, 13.7), so MCUboot's RAM doesn't overlap the ROM's reserved RAM (3.1, 12) |

Boot log on the board (2026-09-29, signed MCUboot + signed led_blinky in the primary slot):

```text
hello sbl.
Bootloader Version 2.2.0
ROM API version 3.0.0
ROM authentication API @0x1300a34f
Image index: 0, Swap type: none
ROM auth: slot 0 check start
ROM auth: slot 0 calling ROM (MBI @0x20400, len 0x2d30)
ROM auth: slot 0 ROM returned status 0x5ac3c35a verified 0x55aacc33
ROM auth: slot 0 MBI @0x20400 verified
ROM auth: slot 0 check start
ROM auth: slot 0 calling ROM (MBI @0x20400, len 0x2d30)
ROM auth: slot 0 ROM returned status 0x5ac3c35a verified 0x55aacc33
ROM auth: slot 0 MBI @0x20400 verified
Bootloader chainload address offset: 0x20000
Reset_Handler address offset: 0x20400
Jumping to the image
```

- `0x5ac3c35a` = `kStatus_SKBOOT_Success`, `0x55aacc33` = `kSECURE_TRACKER_VERIFIED`.
- The primary slot is checked **twice** per boot. The first check comes from the bootstrap logic (`loader.c:2226`: is the primary slot valid, or should the secondary slot be copied?). The second comes from `MCUBOOT_VALIDATE_PRIMARY_SLOT` (`loader.c:2595`).

### 5.6 Build and verification status

| Item | Status |
|---|---|
| Debug build, switch on (MCUXpresso 25.6 toolchain) | ✅ 0 errors, 0 warnings |
| Flash / SRAM (Debug) | 48,344 B / 128 KB (was ≈100 KB with ECDSA). 60,744 B / 232 KB, starting at `0x2000_6000`. After the size reduction (13): ≈ 21.9 KB |
| Flash (Release, after the size reduction, 13) | **12,052 B**. Signed MBI 14,588 B. Tested on the board 2026-09-30 |
| Flash / RAM regions (13.7) | 12,164 B / 32 KB flash, 12,536 B / 16 KB RAM (stack 4 KB, 1,712 B used). Tested on the board 2026-09-30 |
| Signed MCUboot MBI | 50,880 B (`mcuboot_diag_mbi.bin`), signature and chain verified, RKTH `1837ee92…3a48fc` |
| Vector 70 (HASHCRYPT) → `HASHCRYPT_IRQHandler` in `rom_auth.c` | ✅ Checked in the `.axf` |
| No `bootutil_keys` / signature code in the binary | ✅ Checked in the `.map` |
| Run on hardware (LPCXpresso55S69, ROM `K3.0.0`, `open_develop`) | ✅ ROM → MCUboot → ROM auth of the app → led_blinky runs (5.5, 8.1) |
| Release build | ✅ Built in MCUXpresso IDE (2026-09-30), 12,052 B (13.1) |
| Build with the switch **off** (original ECDSA flow) | ⚠️ Not built yet |
| New file in the IDE build | ✅ `rom_auth.c` is picked up by the IDE-generated makefiles |

### 5.7 Known gaps / to do

| # | Gap | Proposal |
|---|---|---|
| G1 | ~~Only the HASHCRYPT clock was enabled before the ROM call~~ | ✅ **Fixed:** `CLOCK_EnableClock(kCLOCK_Casper)` added |
| G2 | The sample keys (`bootutil/nxp_port/keys/*.pem`, `*.c`) are still in the project. They aren't used with the switch on, but a private key is in the source tree | Delete them, or at least the `*-priv*.pem` files, before release |
| G3 | The MBI image-type check only rejects type 0. Other non-signed types (e.g. CRC images) are passed to the ROM | Safe, because success requires `kSECURE_TRACKER_VERIFIED`. Can be made stricter once the exact type values are confirmed in UM11126 |
| G4 | Open points 9.5 (secondary slot), 9.7 (`SEC_BOOT_EN` = 0) | Tests #9 and #11. 9.6 is mitigated (3.1, 12), 9.8 is closed (8.1) |
| G5 | The first boot hang (12) was fixed by two changes together: RAM moved above `0x2000_6000`, and the HASHCRYPT IRQ enabled in the NVIC. Which one was required is not isolated | Optional: build without the NVIC change and boot once. Keep both changes in any case |
| G6 | The ROM authentication runs **twice** per boot (bootstrap check + `MCUBOOT_VALIDATE_PRIMARY_SLOT`) | Acceptable. Measure boot time. If needed, skip the second ROM call when the same slot was already verified in the same boot |
| G7 | The diagnostic log lines (`check start`, `calling ROM`, `ROM returned`) are always printed | Keep them for development. Consider `BOOT_LOG_DBG` for production |

## 6. Build and signing flow

### 6.1 MCUboot (once)

Tools used (verified on 2026-09-29):

| Tool | Path |
|---|---|
| `nxpimage`, `blhost` (SPSDK 3.4.1) | `C:\NXP\SEC_Provi_25.12\bin\_internal\tools\spsdk\` |
| `imgtool` 2.4.0 | `C:\Users\DELL\AppData\Local\Programs\Python\Python312\Scripts\imgtool.exe` (`pip install imgtool`) |
| `arm-none-eabi-objcopy` | MCUXpresso IDE toolchain (`...\ide\plugins\com.nxp.mcuxpresso.tools.win32_*\tools\bin`) |
| Secure Provisioning workspace | `C:\Users\DELL\secure_provisioning` (keys `ROT1` → `IMG1_1` → `IMG1_1_1`, RKTH `1837ee92…3a48fc`) |

1. Build `lpcxpresso55s69_mcuboot_opensource_cm33_core0` (**Release** configuration) with `CONFIG_BOOT_ROM_AUTHENTICATION` on. Use `Release\lpcxpresso55s69_mcuboot_opensource_cm33_core0.axf` (12,052 B, see 13).
2. Sign it as an MBI, with either:
   - the Secure Provisioning Tool: source = MCUboot `.axf`, signed boot type, image key `IMG1_1_1`, **Build image**, or
   - `nxpimage` with `configs\mbi_config.yaml` (`outputImageExecutionAddress: 0x00000000`):
     ```bat
     arm-none-eabi-objcopy -O binary lpcxpresso55s69_mcuboot_opensource_cm33_core0.axf ..\source_images\mcuboot.bin
     cd C:\Users\DELL\secure_provisioning\configs
     nxpimage mbi export -c mbi_config.yaml
     ```
3. First time only: **Write image** in the Secure Provisioning Tool. This also programs CMPA/CFPA (RKTH, boot settings).
4. Later MCUboot updates only (CMPA unchanged): write the signed MBI in ISP mode (see 6.3):
   ```bat
   blhost -p COM4,57600 flash-erase-region 0x0 0xC800
   blhost -p COM4,57600 write-memory 0x0 mcuboot_mbi.bin
   ```
   Erase at least the image size, rounded up to 512 bytes, and never beyond `0x1FFFF`.

> Once MCUboot is signed, don't download it again from the MCUXpresso IDE debugger. Debug access is locked anyway after the ROM boots a signed image (see 12).

### 6.2 Application (every release)

1. Link the app with flash start at **`0x20400`** and size **`0x2FC00`** (slot size minus header).
2. Sign it as an MBI with the **same** certificate chain. Use the Secure Provisioning Tool **Build image only** (not Write), or `nxpimage` with an app config, e.g. `configs\led_blinky_mbi_config.yaml`:
   ```yaml
   family: lpc55s69
   outputImageExecutionTarget: xip
   outputImageAuthenticationType: Signed
   masterBootOutputFile: ../bootable_images/led_blinky_app_mbi.bin
   inputImageFile: ../source_images/led_blinky_app.bin
   outputImageExecutionAddress: 0x00020400
   enableTrustZone: true            # same setting as MCUboot
   certBlock: ../configs/cert_block.yaml
   signer: type=file;file_path=../keys/IMG1_1_1_sha256_2048_65537_v3_usr_key.pem
   ```
   ```bat
   arm-none-eabi-objcopy -O binary led_blinky.axf ..\source_images\led_blinky_app.bin
   nxpimage mbi export -c led_blinky_mbi_config.yaml
   ```
3. Add the MCUboot header (no key):
   ```bat
   imgtool sign --header-size 0x400 --pad-header --align 4 --version 1.0.0 ^
     --slot-size 0x30000 --max-sectors 400 led_blinky_app_mbi.bin led_blinky_app_signed.bin
   imgtool verify led_blinky_app_signed.bin
   ```
   - `--pad-header` is **required**. It puts 0x400 bytes in front of the MBI for the MCUboot header, so the MBI lands at `0x20400`, its link address. *(An earlier version of this document said the opposite. That was wrong.)*
   - Don't pass `--key`. MCUboot only checks the SHA-256 TLV; the signature is checked by the ROM.
4. Program `led_blinky_app_signed.bin` **at the slot start**:
   - Primary slot: at **`0x20000`** (not `0x20400`):
     ```bat
     blhost -p COM4,57600 flash-erase-region 0x20000 0x30000
     blhost -p COM4,57600 write-memory 0x20000 led_blinky_app_signed.bin
     ```
   - Field update: at `0x50000` (secondary slot). MCUboot installs it after the ROM check passes.

### 6.3 Programming in ISP mode

| Item | Value |
|---|---|
| Enter ISP | Hold **ISP**, press **Reset**, release |
| Interface used | **UART through the LPC-Link2 VCOM (COM4)**, 57600 baud: `blhost -p COM4,57600 ...`. USB ISP (`-u 0x1fc9,0x0021`) needs the MCU's own USB port connected |
| Check connection first | `blhost -p COM4,57600 get-property 1` → `Current Version = K3.0.0` |
| COM4 in use | Close the serial terminal first. Otherwise `Access denied` and nothing is written |
| Verify after writing | `blhost -p COM4,57600 read-memory <addr> <len> readback.bin`, then compare with the file |
| Back to normal boot | Reset **without** holding ISP |

## 7. Security checklist before production

- [ ] Sample MCUboot keys removed from `bootutil/nxp_port/keys/` (G2 in 5.7). Private RSA keys of the Secure Provisioning workspace (`keys/`) kept offline or in an HSM, not in the project folder.
- [x] `MCUBOOT_VALIDATE_PRIMARY_SLOT` enabled: the app is authenticated on every boot. *(done, 5.2)*
- [x] Hook returns `FIH_BOOT_HOOK_REGULAR` on success, so both the ROM and MCUboot checks run. *(done, 5.4)*
- [x] HASHCRYPT interrupt routed to the ROM during the call. *(done, 5.3)*
- [x] MCUboot halts if the ROM authentication API is missing. *(done, 5.5)*
- [x] CASPER clock enabled explicitly before the ROM call. *(done, 5.3)*
- [x] MCUboot RAM doesn't overlap the ROM reserved RAM `0x2000_0000`–`0x2000_5FFF`. *(done, 3.1)*
- [x] HASHCRYPT IRQ enabled in the NVIC during the ROM call. *(done, 5.3)*
- [ ] Release build checked, and flash/RAM use recorded.
- [ ] Firmware version and anti-rollback: the image version fits the CFPA `SECURE_FW_VERSION` scheme.
- [ ] Debug access locked (`DCFG_CC_SOCU` in CMPA) or protected by Debug Authentication (`debug_auth/`).
- [ ] Full test done in life cycle `open_develop`, including rejection of a tampered/unsigned MCUboot **and** app.
- [ ] Only then: life cycle `closed_deploy_infield` and **seal CMPA** (irreversible).
- [ ] Recovery path (ISP) policy decided before sealing.

## 8. Test plan

| # | Test | Expected result |
|---|---|---|
| 1 | Correctly signed MCUboot + app | Boots to the app, log shows the ROM check passed |
| 2 | Unsigned or modified MCUboot | ROM does not boot (enters ISP) |
| 3 | App signed with a different key/cert | `ROM authentication failed`, app not started |
| 4 | App with 1 byte modified | ROM check or hash check fails, app not started |
| 5 | Valid update in secondary slot | Installed to primary, verified, booted |
| 6 | Invalid update in secondary slot | Rejected; primary app still boots |
| 7 | Older firmware version (rollback) | Rejected by the version policy |
| 8 | `rom_auth_init()` log at boot | `ROM API version x.y.z` and `ROM authentication API @0x130xxxxx` (inside `0x1300_0000`–`0x1301_FFFF`) |
| 8a | Unsigned (plain) MBI in the primary slot | `ROM auth: slot 0 holds an unsigned MBI`, not booted |
| 8b | App signed with `imgtool` only (no MBI inside) | Rejected (type or length check, or ROM failure), not booted |
| 8c | MBI truncated (partly erased pages) | `ROM auth: slot N MBI region not fully programmed`, no bus fault |
| 9 | Authenticate the image in the secondary slot (`0x50400`), linked for `0x20400` | Must pass. If it fails, see 9.5 |
| 10 | RAM pattern test: fill unused SRAM, call the API, compare | Know which RAM the ROM uses (see 9.6) |
| 11 | Signed app, but `SEC_BOOT_EN` not yet set in CMPA | Record behaviour (see 9.7) |

### 8.1 Results

Board LPCXpresso55S69, ROM `K3.0.0` / API `3.0.0`, life cycle `open_develop`, CMPA not sealed.

| Date | # | Test | Result |
|---|---|---|---|
| 2026-09-27 | – | Unsigned hello_app (raw `.bin`, no MCUboot header) at `0x20000` | ✅ Rejected by MCUboot's header check (`Image in the primary slot is not valid!` ×2, `Unable to find bootable image`). The ROM call is not reached |
| 2026-09-27 | 8 | `rom_auth_init()` log | ✅ `ROM API version 3.0.0`, `ROM authentication API @0x1300a34f` (inside ROM) |
| 2026-09-29 | – | Signed MCUboot MBI checked offline (`bootable_images\`) | ✅ Length, type `0x4`, exec address `0x0`, 3-certificate chain ROT1 → CA → IMG, RSA signature valid, RKTH `1837ee92…3a48fc` |
| 2026-09-29 | – | Signed led_blinky, first MCUboot build (RAM at `0x2000_0000`) | ❌ Hang after `Image index: 0, Swap type: none`, no `ROM auth:` log (see 12) |
| 2026-09-29 | **1** | Signed MCUboot (RAM at `0x2000_6000`, NVIC fix) + signed led_blinky | ✅ `status 0x5ac3c35a verified 0x55aacc33`, `Jumping to the image`, **LED blinks** |
| 2026-09-29 | 9 (primary) | MBI at `0x20400` linked for `0x20400` | ✅ Accepted. The secondary slot (`0x50400`) is still open |
| 2026-09-30 | 1 | Size-reduced MCUboot (TinyCrypt SHA-256, tiny logger, no Redlib stdio init, 14,588 B signed) + signed led_blinky | ✅ Same log as before, `status 0x5ac3c35a verified 0x55aacc33`, `Jumping to the image` (13) |
| 2026-09-30 | – | MCUboot flash region 32 KB, SRAM 16 KB, heap 0, stack 4 KB (13.7) | ✅ Same log, `Stack used: 1712 of 4096 bytes` |
| – | 2, 3, 4, 5, 6, 7, 8a–8c, 9 (secondary), 10, 11 | – | ⏳ Not run yet |

## 9. Detailed analysis of `skboot_authenticate()`

**Question:** Is `skboot_authenticate()` really the Boot ROM's own image authentication, and can MCUboot call it?

**Short answer:** Yes. The SDK code shows it is a function pointer inside the Boot ROM, and the SDK comment says the ROM boot itself uses it. There are, however, **7 integration points** that must be handled or verified on hardware. They are listed below.

### 9.1 Call path (verified in the source code)

SDK path (`fsl_iap.c`), used here as the **evidence**:

```
drivers/fsl_iap.c:665   skboot_authenticate(imageStartAddr, &isSignVerified)
      │   return BOOTLOADER_API_TREE_POINTER->skbootAuthenticate->skboot_authenticate_function(...)
      ▼
ROM  0x130010f0   bootloader_tree_t (ROM API tree, read-only, inside the Boot ROM)
      │   .skbootAuthenticate  ──►  skboot_authenticate_interface_t (in ROM)
      ▼
ROM  skboot_authenticate_function()   ← same code the ROM runs at reset for the boot image
```

Path **implemented** in this project (`rom_auth.c` uses the same ROM tree directly, with pointer checks):

```
bootutil_hooks.c   boot_image_check_hook()
      │   FIH_CALL(rom_auth_check_slot, ...)
      ▼
rom_auth.c         rom_auth_check_slot()      header / MBI pre-checks (5.3)
      │   ROM_API_TREE->skbootAuthenticate->skboot_authenticate_function(mbi_addr, &verified)
      ▼
ROM  0x130010f0 → skboot_authenticate_function()   (pointers checked by rom_auth_init() at boot)
```

Evidence:

| Evidence | Location | What it shows |
|---|---|---|
| `#define BOOTLOADER_API_TREE_POINTER ((bootloader_tree_t *)0x130010f0U)` for `LPC55S69_cm33_core0_SERIES` | `drivers/fsl_iap.c:32` | The API table is at a fixed address **inside the Boot ROM** (ROM = `0x1300_0000`) |
| `const skboot_authenticate_interface_t *skbootAuthenticate; /*!< Image authentication API. */` | `drivers/fsl_iap.c:197` | The ROM tree publishes an image-authentication entry |
| `skboot_status_t (*skboot_authenticate_function)(const uint8_t *imageStartAddr, secure_bool_t *isSignVerified);` | `drivers/fsl_iap.c:175` | The SDK function is only a thin wrapper. The code runs in ROM |
| `"This is called by ROM boot or by ROM API g_skbootAuthenticateInterface"` | `drivers/fsl_iap_skboot_authenticate.h` | The same function is used by the ROM's own boot flow |
| `"Authenticate entry function with ARENA allocator init"` | same header | The function sets up its own internal memory allocator (see 9.6) |
| `skboot_authenticate` and `HASH_IRQHandler` in the link map | `Debug/*.map` | `fsl_iap.c` is already compiled and linked into this project |

The same ROM tree provides the flash driver (`FLASH_Init`, `FLASH_Erase`, …) that MCUboot **already uses** through `mflash_drv.c`. So calling into this ROM table from MCUboot is already proven in this project.

### 9.2 ROM API tree layout

```c
typedef struct BootloaderTree            /* at 0x130010f0 in ROM (LPC55S69) */
{
    void (*runBootloader)(void *arg);                          /* +0x00 */
    standard_version_t bootloader_version;                     /* +0x04 */
    const char *copyright;                                     /* +0x08 */
    const uint32_t reserved0;                                  /* +0x0C */
    flash_driver_interface_t flashDriver;                      /* +0x10 */
    const kb_interface_t *kbApi;                               /* +0x14 */
    const uint32_t reserved1[4];                               /* +0x18 */
    const skboot_authenticate_interface_t *skbootAuthenticate; /* +0x28 */
} bootloader_tree_t;

typedef struct _skboot_authenticate_interface
{
    skboot_status_t (*skboot_authenticate_function)(const uint8_t *imageStartAddr,
                                                    secure_bool_t *isSignVerified);
    void (*skboot_hashcrypt_irq_handler)(void);
} skboot_authenticate_interface_t;
```

The source says: *"The order of existing fields must not be changed."* The layout is a fixed ROM contract.

### 9.3 Return values: two "secure" constants

| Output | Success value | Other values |
|---|---|---|
| Return `skboot_status_t` | `kStatus_SKBOOT_Success = 0x5ac3c35a` | `Fail 0xc35ac35a`, `InvalidArgument 0xc35a5ac3`, `KeyStoreMarkerInvalid 0xc3c35a5a`, `HashcryptFinishedWithStatusFail 0xc15a5acb` |
| `*isSignVerified` (`secure_bool_t`) | `kSECURE_TRACKER_VERIFIED = 0x55aacc33` | `kSECURE_FALSE 0x5aa55aa5`, `kSECURE_TRUE 0xc33cc33c` (**not** the same as verified) |

- The values have a large Hamming distance, so a single bit flip or a glitch cannot turn failure into success. **Never** test with `if (status)` or `if (verified)`. Always compare against the exact constants.
- **Both** outputs must match. `kSECURE_TRUE` is **not** sufficient: only `kSECURE_TRACKER_VERIFIED` means the signature was checked.
- `verified` must be initialised to `kSECURE_FALSE` before the call.

### 9.4 Integration point 1: HASHCRYPT interrupt conflict (found in this project)

The ROM exports its own HASHCRYPT interrupt handler (`skboot_hashcrypt_irq_handler`), so the ROM code may use the HASHCRYPT interrupt while it hashes the image. In this project:

| Symbol | Where | Problem |
|---|---|---|
| Vector 70 → `HASHCRYPT_IRQHandler` (WEAK) → `HASHCRYPT_DriverIRQHandler()` | `startup/startup_lpc55s69_cm33_core0.c:319, 733` | The vector calls the **SDK** driver handler |
| `HASHCRYPT_DriverIRQHandler()` | `drivers/fsl_hashcrypt.c:1795` | Uses the SDK context `s_ctx`, **not** the ROM's. Wrong or NULL context if the ROM triggers it |
| `HASH_IRQHandler()` → ROM handler | `drivers/fsl_iap.c:675` | **Not** in the vector table (the name doesn't match), so it is never called |

**Fix (implemented in `rom_auth.c`, see 5.3):** a strong `HASHCRYPT_IRQHandler` calls the ROM's `skboot_hashcrypt_irq_handler` directly while the ROM call is active (`s_rom_auth_active`), and the SDK handler otherwise. In addition, `HASHCRYPT_IRQn` is **enabled in the NVIC** during the call (and restored afterwards). Without that, a ROM that waits for the HASHCRYPT interrupt would wait forever, because nothing else in MCUboot enables it. Also ensure that no PSA/HASHCRYPT operation is in progress when the ROM is called. MCUboot is single-threaded, so this holds when the call is made from the hook.

### 9.5 Integration point 2: image position vs. execution address (MUST test)

- The ROM normally authenticates an image that sits at its own execution address (MCUboot at `0x0`).
- In this design:
  - Primary slot: MBI at `0x20400`, linked for `0x20400` → **same address**. OK.
  - Secondary slot: MBI at `0x50400`, but linked for `0x20400` → **different address**.
- The MBI header stores the image length (`+0x20`), type (`+0x24`), certificate offset (`+0x28`, relative to the image start) and execution address (`+0x34`). The signature covers these bytes. The certificate offset is relative, so parsing works at any address. **Whether the ROM rejects an XIP image whose `+0x34` address does not match `imageStartAddr` is not documented in the SDK and must be tested (test #9).**

If the secondary-slot check fails for that reason, choose one of:

| Option | How | Trade-off |
|---|---|---|
| A | Keep ROM check on **both** slots (preferred, if test #9 passes) | None |
| B | Secondary slot: MCUboot hash check only. Primary slot: ROM check before every boot | A bad-signature update overwrites the good app (overwrite-only). The device doesn't run bad code but has no app (DoS). Recover via ISP / new update |
| C | Copy the candidate MBI to a RAM buffer or scratch area at the expected address and authenticate there | Costs RAM/flash, more code |

### 9.6 Integration point 3: RAM used by the ROM API

- The header states the function initialises an **ARENA allocator**, so the ROM needs working RAM during the call (certificate parsing, RSA, hash context).
- It is not documented in the SDK which RAM region the ROM uses at run time, or whether it uses the caller's stack.
- **Finding (2026-09-29):** the ROM reports its reserved RAM through the ISP command `blhost get-property 12`:

  | Reserved region | Size |
  |---|---|
  | `0x1400_0000` – `0x1400_5FFF` | 24 KB |
  | `0x0400_0000` – `0x0400_7FFF` (SRAMX) | 32 KB |
  | `0x3000_0000` – `0x3000_5FFF` (secure alias of SRAM) | 24 KB |
  | `0x2000_0000` – `0x2000_5FFF` (SRAM) | 24 KB |

- The original MCUboot RAM layout put `.data` + `.bss` (≈ 20 KB, including the debug console state and `s_rom_auth_active`) at `0x2000_0000`, **inside** that area. With that layout the boot hung during the first ROM call (12).
- **Implemented:** MCUboot SRAM now starts at **`0x2000_6000`** (3.1). With this change (plus the NVIC fix) the ROM call returns and the boot continues.
- **Still open (test #10):** this list is what the ROM *bootloader* reserves. Confirming that `skboot_authenticate()` itself stays inside it needs the RAM pattern test. Keep enough stack (≥ 4 KB) for the call.

### 9.7 Integration point 4: behaviour depends on CMPA/CFPA

The ROM function reads the protected flash area:
- **RKTH** in CMPA: the root of trust.
- **Revocation bits** and **firmware version** in CFPA: anti-rollback and key revocation.
- **Secure boot settings** (`SEC_BOOT_EN`) in CMPA.

What to verify (test #11):
- With `SEC_BOOT_EN` = 0 (development), the result for a signed and an unsigned image.
- The production decision must rely **only** on `kStatus_SKBOOT_Success` **and** `kSECURE_TRACKER_VERIFIED`. Never accept an image because "secure boot is off".

### 9.8 Integration point 5: ROM revision

- LPC55S69 exists in silicon revisions **0A** and **1B**. `fsl_iap.c` handles two ROM flash-API versions (`get_rom_api_version()`: ROM major version 3 = new API), and has separate flash-read addresses for REV0/REV1 (`LPC55S69_REV0_FLASH_READ_ADDR`, `LPC55S69_REV1_FLASH_READ_ADDR`).
- `skbootAuthenticate` is at a fixed position in the tree, but its presence on the old revision is not guaranteed by the SDK.
- **Implemented:** `rom_auth_init()` (5.3) runs at every boot. If the pointers aren't inside ROM, `sbl_boot_main()` halts (fail closed). Use 1B silicon for production, and record the logged ROM version in test #8.
- **Result (test #8):** the board reports ROM API `3.0.0` (ISP `K3.0.0`), and `skboot_authenticate_function` is at `0x1300a34f`, inside ROM.

### 9.9 Integration point 6: security state and caller

- The ROM API reads CMPA/CFPA and uses HASHCRYPT/CASPER, so it must be called from **secure** state. MCUboot runs secure (reset state), so this holds.
- It must be called **before** jumping to the app, from MCUboot only. The app itself should not be able to use it to replace the boot decision.
- The ROM uses **HASHCRYPT and CASPER**. MCUboot's PSA driver uses the same hardware (`component/psa_crypto_driver/hashcrypt`, `casper`). Calls are sequential, so there's no conflict, but don't call the ROM from inside a PSA operation. `rom_auth.c` enables only the HASHCRYPT clock. The CASPER clock is currently enabled indirectly by `psa_crypto_init()` → `casper_common_init()` → `CASPER_Init()` (see G1 in 5.7).

### 9.10 Integration point 7: MCUboot configuration side effects

| Item | Why | Status |
|---|---|---|
| `MCUBOOT_IMAGE_ACCESS_HOOKS` must be defined | Otherwise `boot_image_check_hook()` is never called (`bootutil/include/bootutil/boot_hooks.h:55`) | ✅ 5.2 |
| `MCUBOOT_VALIDATE_PRIMARY_SLOT` must be defined manually | It is normally tied to `CONFIG_BOOT_SIGNATURE`, which is turned off in this design | ✅ 5.2 |
| Hook returns `FIH_BOOT_HOOK_REGULAR` on success | `loader.c:1095-1099`: MCUboot then also runs its own hash/TLV check | ✅ 5.4 |
| Hook returns `FIH_FAILURE` on any failure | Image is rejected. Fail closed | ✅ 5.4 |
| No MCUboot public key compiled in | Otherwise `keys.c` fails with `#error`, or a key that is no longer the trust anchor stays in the binary | ✅ 5.5 |

### 9.11 Summary: confidence level

| Statement | Status |
|---|---|
| `skboot_authenticate()` runs code inside the Boot ROM | ✅ Verified in SDK source (`fsl_iap.c:32, 175, 197, 665`) |
| It is the same function the ROM uses at reset | ✅ Stated by the SDK header |
| It checks the NXP MBI certificate chain against the RKTH in CMPA | ✅ **Confirmed on hardware** (test #1: `0x5ac3c35a` / `0x55aacc33`). Negative tests #3, #4 still to run |
| It is callable from MCUboot (secure, after reset) | ✅ Same ROM tree as the flash API MCUboot already uses |
| HASHCRYPT IRQ routing is correct in this project | ✅ Fixed in `rom_auth.c` (strong `HASHCRYPT_IRQHandler` + NVIC enable during the call, see 5.3). It was wrong in the SDK default |
| Works for an image at its execution address (primary slot `0x20400`) | ✅ Confirmed on hardware |
| Works for an image outside its execution address (secondary slot) | ⚠️ Unknown. Test #9 |
| RAM used by the ROM during the call | ✅ Mitigated: MCUboot RAM moved above the ROM reserved area (9.6). ⚠️ Exact use during the call: test #10 |
| Present on this silicon (ROM API 3.0.0) | ✅ Confirmed (test #8). Checked at run time on every boot (9.8) |
| Behaviour when `SEC_BOOT_EN` = 0 | ⚠️ Test #11 |

**Conclusion:** the approach works on hardware. The ROM authenticates MCUboot at reset, and MCUboot authenticates the application through `skboot_authenticate()` against the same RKTH. Two integration defects were found and fixed: HASHCRYPT IRQ routing/enable (9.4) and the RAM overlap with the ROM reserved area (9.6, 12). Open: secondary slot (#9), RAM pattern test (#10), `SEC_BOOT_EN` = 0 behaviour (#11), and the negative tests (#2–#4, #6).

## 10. Running MCUboot in the Secure world (TrustZone)

**Question:** Can MCUboot run in the Secure world, as part of the root of trust?

**Short answer:** Yes. MCUboot **already runs in Secure state** today, and it must, because the ROM authentication API only works from Secure state. What TrustZone adds is the job of **protecting** MCUboot and the Secure world from the Non-secure application. That job belongs to the Secure application that MCUboot starts, not to MCUboot itself.

### 10.1 Why MCUboot is already Secure (verified in this project)

| Fact | Evidence |
|---|---|
| Cortex-M33 with TrustZone always leaves reset in **Secure** state | Armv8-M architecture |
| The Boot ROM runs Secure and enters MCUboot in Secure state | ROM boot flow |
| MCUboot never switches to Non-secure | Built without `-mcmse` (not in `.cproject`), no `BXNS` / `cmse_nsfptr` in the code |
| The jump to the app keeps Secure state | `do_boot()` in `bootutil/nxp_port/boot.c`: `__set_CONTROL(0)`, `__set_MSP()`, plain branch to the reset vector |
| `SystemInit()` doesn't configure the SAU for this build | `device/system_LPC55S69_cm33_core0.c:246-254`: the TrustZone code is under `__ARM_FEATURE_CMSE == 3`, which is not set |

**Note on link addresses:** MCUboot is linked at the Non-secure **aliases** (flash `0x00000000`, SRAM `0x20000000`, see the `.map` file). This doesn't make it Non-secure. After reset the SAU is disabled with `ALLNS = 0`, so **every address is Secure**. The two aliases reach the same physical memory.

| Memory | Non-secure alias | Secure alias |
|---|---|---|
| Internal flash | `0x0000_0000` | `0x1000_0000` |
| SRAM | `0x2000_0000` | `0x3000_0000` |
| Peripherals (e.g. `FLASH` controller) | `0x4003_4000` | `0x5003_4000` |
| `AHB_SECURE_CTRL` | `0x400A_C000` | `0x500A_C000` |

### 10.2 Recommended boot chain with TrustZone

```
 Reset
   │
   ▼
 Boot ROM (S) ──auth: RKTH in CMPA──► MCUboot (S)          0x1000_0000 – 0x1001_FFFF
                                        │  auth: ROM API skboot_authenticate()
                                        ▼
                                      Secure app (S)       0x1002_0400 (primary slot)
                                        │  1. SAU + AHB secure controller + NVIC ITNS
                                        │  2. lock the security configuration
                                        │  3. BXNS
                                        ▼
                                      Non-secure app (NS)  NS flash / NS SRAM
```

Rules:

1. **MCUboot stays Secure and simple.** It authenticates and jumps. It does **not** enable the SAU, and it never jumps to Non-secure code directly.
2. **MCUboot's image must be the Secure application.** Only the Secure app knows the memory split, so it configures the SAU, the AHB secure controller rules and the interrupt target states (`NVIC->ITNS`), and then starts the Non-secure app with `BXNS`.
3. **Include the Non-secure app in the authenticated image.** Build Secure + Non-secure as **one combined image** in the primary slot, so a single ROM check covers both. Alternatively, use two MCUboot images (`MCUBOOT_IMAGE_NUMBER = 2`), where each one gets its own ROM check.
4. **Link the Secure app to the Secure alias** (`0x1002_0400`). MCUboot reads the vector table through `0x0002_0400`, the same physical flash. The reset vector holds a `0x1002_xxxx` address, so the branch lands in Secure code.

### 10.3 Impact on Option B (ROM authentication)

| Item | Effect |
|---|---|
| `skboot_authenticate()` must be called from Secure state | ✅ MCUboot is Secure |
| HASHCRYPT / CASPER / flash controller accessed through NS alias addresses | ✅ Works while the SAU is disabled (everything is Secure) |
| MBI execution address | Sign the MBI for the address the image is **linked** at. For a Secure app that's `0x1002_0400`, while `skboot_authenticate()` receives the `0x0002_0400` alias. **Add this to test #9** (image address ≠ execution address) |
| MBI image type | A Secure (TrustZone-enabled) image may need a different MBI image type / TrustZone preset (`trustzone_files/` in the Secure Provisioning workspace). Use the TrustZone option of the Secure Provisioning Tool when signing the application |
| Secondary slot | The same address question as 9.5, now also with the Secure alias |

### 10.4 Protecting MCUboot (the root of trust)

Secure state alone doesn't protect MCUboot's flash. The LPC55S69 has **no hardware write protection** for the MCUboot region, so any code with access to the flash controller can erase it. The protections come from the Secure app's TrustZone configuration:

| # | Protection | How (done by the Secure app, before `BXNS`) |
|---|---|---|
| P1 | Non-secure code can't program or erase flash | AHB secure controller: set the `FLASH` controller rule (`SEC_CTRL_APB_BRIDGE1_MEM_CTRL2.FLASH_CTRL_RULE`) to Secure-privileged only |
| P2 | Non-secure code can't read or execute MCUboot or the Secure app | SAU + AHB secure controller flash rules: mark `0x0000_0000 – end of Secure app` as Secure |
| P3 | Non-secure code can't change the security configuration | Set `MISC_CTRL_REG.WRITE_LOCK`, `MISC_CTRL_DP_REG.WRITE_LOCK` and the `MASTER_SEC_LEVEL` lock. Once locked, they stay locked until the next reset |
| P4 | Security checking really enforced | `MISC_CTRL_REG` / `MISC_CTRL_DP_REG`: enable secure checking and privilege checking, and disable the "allow all" defaults |
| P5 | Firmware update can't touch MCUboot | Only a **Secure** update service writes flash, and only to the secondary slot (`0x0005_0000 – 0x0007_FFFF`). The NS app only hands over the received data |
| P6 | Debug can't bypass TrustZone | CMPA `DCFG_CC_SOCU`: disable secure debug (and NS debug if not needed), or protect it with Debug Authentication |
| P7 | Last line of defence | If MCUboot is modified anyway, the ROM detects it at the next reset and doesn't boot it. That's a denial of service, not a compromise |

**Remaining risk:** code running in the **Secure** world (MCUboot, Secure app) can still erase MCUboot. Keep the Secure app small, review it, and authenticate it (as Option B does).

### 10.5 Optional: link MCUboot to the Secure aliases

This isn't required for it to work, but it makes the map file match reality and avoids confusion once the Secure app enables the SAU.

| Region | Today | Optional |
|---|---|---|
| `PROGRAM_FLASH` | `0x0000_0000`, 128 KB | `0x1000_0000`, 128 KB |
| `SRAM` | `0x2000_0000`, 256 KB | `0x3000_0000` (reduce it to leave room for the Secure app's RAM plan) |
| `BOOT_FLASH_BASE` (`flash_partitioning.h`) | `0x0000_0000` | Keep `0x0` for flash offsets. Only the linker changes |

If you do it, check `do_boot()`: `flash_base + br_image_off` must still point to the application vector table (either alias works for reading).

### 10.6 TrustZone test plan

| # | Test | Expected result |
|---|---|---|
| T1 | Read `CONTROL`, `IPSR` and the security state at MCUboot entry and just before `do_boot()` | Secure state (e.g. `TT` instruction / `__TZ_get_*` read works, SAU `CTRL.ENABLE = 0`) |
| T2 | Secure app at `0x1002_0400` booted by MCUboot | Secure app runs, configures SAU, starts NS app |
| T3 | NS app tries to erase `0x0000_0000` (MCUboot) | SecureFault / BusFault. Flash unchanged |
| T4 | NS app tries to read MCUboot / Secure app flash | SecureFault |
| T5 | NS app tries to write `AHB_SECURE_CTRL` after the locks | No effect or fault. Settings unchanged |
| T6 | Update through the Secure update service | Written only to the secondary slot, then installed by MCUboot after the ROM check |
| T7 | ROM auth of the Secure app signed for `0x1002_0400` (extends test #9) | `kStatus_SKBOOT_Success` + `kSECURE_TRACKER_VERIFIED` |


## 11. Change history (git)

The project folder is a local git repository. Use VS Code **Source Control** / **Timeline** to see a side-by-side diff of any change.

| Commit | Content |
|---|---|
| `b0c3cd3` | Baseline: unmodified `mcuboot_opensource` example from MCUXpresso SDK 25.9.0 (`lpcxpresso55s69`) |
| `aa7b59c` | Option B: `CONFIG_BOOT_ROM_AUTHENTICATION`, `rom_auth.c/.h`, hook, `boot.c`, `keys.c`, `mcuboot_config.h`, this document |
| `9a66e72` | Bring-up fix (12): `rom_auth.c` (NVIC enable, CASPER clock, log lines), `.cproject` (SRAM at `0x20006000`) |
| `f20e167` | Document: Section 10 (TrustZone), Sections 5–9 aligned with the source code, 3.1, 6.3, 8.1, 12 |
| `9d5dbdb` | Size reduction to 12,052 B (13): TinyCrypt SHA-256, tiny UART logger, startup without Redlib stdio init, build excludes, SRAM restored + RAM overlap check |
| `c4852b9` | Flash region 32 KB, SRAM 16 KB, heap 0, stack 4 KB, stack measurement (13.7) |

Files changed against the SDK baseline:

| File | Section |
|---|---|
| `source/sblconfig.h` | 5.1 |
| `bootutil/nxp_port/include/mcuboot_config/mcuboot_config.h` | 5.2 |
| `bootutil/nxp_port/rom_auth.c`, `rom_auth.h` (new) | 5.3 |
| `bootutil/nxp_port/bootutil_hooks.c` | 5.4 |
| `bootutil/nxp_port/boot.c`, `bootutil/nxp_port/keys.c` | 5.5 |
| `.gitignore` (new) | Build output excluded from git |
| `.cproject` (SRAM memory region) | 3.1, 12 |
| `.cproject` (build folders/excludes, `SDK_DEBUGCONSOLE=2`) | 13.3 |
| `bootutil/nxp_port/sbl_log.c`, `sbl_log.h` (new), `mcuboot_logging.h`, `source/main.c` | 13.4 |
| `bootutil/nxp_port/tinycrypt/`, `bootutil/nxp_port/include/tinycrypt/` (new), `source/sblconfig.h` | 13.2 |
| `startup/startup_lpc55s69_cm33_core0.c` | 13.2 |

## 12. Hardware bring-up record (2026-09-29)

### 12.1 Setup

| Item | Value |
|---|---|
| Board | LPCXpresso55S69, ROM `K3.0.0` (ROM API `3.0.0`) |
| Life cycle | `open_develop`, CMPA not sealed |
| MCUboot | Signed MBI at `0x0` (Secure Provisioning workspace `C:\Users\DELL\secure_provisioning`, key `IMG1_1_1`) |
| Application | `lpcxpresso55s69_led_blinky_lpc_cm33_core0`, linked at `0x20400`, signed MBI (`led_blinky_mbi_config.yaml`) + `imgtool --pad-header`, written at `0x20000` |
| Programming | ISP over UART, `blhost -p COM4,57600`, read-back compared |

### 12.2 Symptom

The first MCUboot build (RAM at `0x2000_0000`) stopped after:

```text
ROM authentication API @0x1300a34f
Image index: 0, Swap type: none
```

There was no `ROM auth:` line and no LED activity. The image passed MCUboot's header check, so MCUboot entered `rom_auth_check_slot()`, but neither a success nor a failure log came out.

### 12.3 Analysis

| Step | Finding |
|---|---|
| Attach the debugger (LinkServer gdbserver) | ❌ `Cannot find MEM-AP`: the SWD debug port answers, but the access port is locked after the ROM has booted a signed image. Debugging in this state needs Debug Authentication (`debug_auth/`) or a change of `DCFG_CC_SOCU` |
| Confirm the flashed MCUboot matches the `.axf` | ✅ Identical, except for the MBI header words |
| ROM reserved RAM (`blhost get-property 12`) | `0x2000_0000`–`0x2000_5FFF` is reserved by the ROM. **All** MCUboot `.data`/`.bss` was inside it (`s_rom_auth_active` at `0x2000_4d44`) |
| NVIC state | Nothing in MCUboot enables `HASHCRYPT_IRQn`, although the ROM provides a HASHCRYPT interrupt handler |

Two likely causes:
1. The ROM uses its reserved RAM as working memory and overwrites MCUboot's variables, including the UART console state, so no further log appears.
2. The ROM waits for a HASHCRYPT interrupt that is never enabled.

### 12.4 Fix

| Change | File |
|---|---|
| MCUboot SRAM moved to `0x2000_6000` / `0x3a000` | `.cproject` (memory configuration) → generated `Debug/*_Debug_memory.ld` |
| `HASHCRYPT_IRQn` enabled in the NVIC during the ROM call, then restored | `bootutil/nxp_port/rom_auth.c` |
| CASPER clock enabled before the call (G1) | `bootutil/nxp_port/rom_auth.c` |
| Log lines before/after the ROM call and on all failure paths | `bootutil/nxp_port/rom_auth.c` |

### 12.5 Result

The ROM returned `kStatus_SKBOOT_Success` (`0x5ac3c35a`) and `kSECURE_TRACKER_VERIFIED` (`0x55aacc33`) for both checks. MCUboot jumped to `0x20400`, and **the LED blinks**. The full log is in 5.5.

Which of the two fixes was strictly required hasn't been isolated yet (G5). Both stay in the design.

### 12.6 Lessons

1. Any code that calls the LPC55S69 ROM API must keep its RAM out of the ROM reserved area (`blhost get-property 12`).
2. Put a log line before and after every ROM call. Otherwise a hang inside the ROM looks the same as a silent failure.
3. After the ROM boots a signed image, SWD debug is locked. Plan for UART logs or Debug Authentication when debugging secured boards.
4. `imgtool` needs `--pad-header` when the MBI is linked right after the header (6.2).
5. Flash the application at the **slot start** (`0x20000`), not at its link address (`0x20400`).

## 13. Size reduction (2026-09-30)

Goal: MCUboot code in the 10–20 KB range.

### 13.1 Result

| Build | Before | After |
|---|---|---|
| Release (`-Os`, `NDEBUG`), built in MCUXpresso IDE | 24,892 B (measured) | **12,052 B** (12,000 code + 52 data) |
| Debug (`-O0`) | 48,344 B | ≈ 21.9 KB |
| Signed MCUboot MBI (code + certificate block + signature) | 50,880 B | **14,588 B** |

Tested on the board on 2026-09-30: the ROM authenticates MCUboot, MCUboot authenticates led_blinky through the ROM (twice), `Jumping to the image`, and the LED blinks. The log output is identical to the previous build (5.5).

The IDE Release build contains the same 113 functions and data objects, with the same sizes, as the build tested on the board. Only the link order differs.

### 13.2 What was removed or replaced

| # | Change | Files | Saved (`-Os`) |
|---|---|---|---|
| 1 | **SHA-256 with TinyCrypt** instead of PSA Crypto / mbedTLS. With ROM authentication, MCUboot only needs SHA-256. AES, CTR-DRBG, entropy, the PSA key store and the CASPER/HASHCRYPT PSA drivers were only linked because of `psa_crypto_init()` | `source/sblconfig.h` (`CONFIG_BOOT_USE_TINYCRYPT`), new `bootutil/nxp_port/tinycrypt/sha256.c`, `utils.c`, `bootutil/nxp_port/include/tinycrypt/*.h` (copied from the SDK TinyCrypt) | ≈ 9 KB |
| 2 | **Tiny polled UART logger** instead of the SDK debug console (serial manager, UART adapter, `fsl_str` printf) | New `bootutil/nxp_port/sbl_log.c` / `.h`, `mcuboot_logging.h`, `source/main.c`, `SDK_DEBUGCONSOLE=2` | ≈ 3 KB |
| 3 | **Startup calls `main()` directly.** Redlib's `__main()` only adds stdio initialisation (`_initio`, `fseek`, `fflush`…), and MCUboot uses no stdio | `startup/startup_lpc55s69_cm33_core0.c` | ≈ 1.2 KB |
| 4 | **Unused drivers out of the build.** `fsl_flexcomm.c` kept its FLEXCOMM IRQ handlers in the vector table | `.cproject` | ≈ 0.5 KB |
| 5 | HASHCRYPT IRQ handler only forwards to the ROM, no SDK HASHCRYPT driver | `bootutil/nxp_port/rom_auth.c` | small |

### 13.3 Build configuration (`.cproject`, Debug and Release)

| Setting | Value |
|---|---|
| Source folders no longer built | `component/` (lists, psa_crypto_driver, rng, serial_manager, uart), `mbedtls3x/`, `mbedtls_config/` |
| Excluded in `utilities/` | `debug_console/`, `str/` |
| Excluded in `drivers/` | `fsl_casper.c`, `fsl_hashcrypt.c`, `fsl_flexcomm.c`, `fsl_usart.c`, `fsl_i2c.c`, `fsl_spi.c`, `fsl_gpio.c` |
| Defines | `SDK_DEBUGCONSOLE=2` (SDK `PRINTF` becomes a no-op, e.g. in `fsl_power.c`) |
| SRAM | `0x20006000` / `0x4000` (16 KB, 3.1, 13.7). Flash `0x0` / `0x8000` (32 KB). Heap `0x0`, stack `0x1000` |

The files stay on disk. Only the build uses less.

### 13.4 Tiny logger (`sbl_log.c`)

| Item | Value |
|---|---|
| UART | FLEXCOMM0 / USART0, FRO 12 MHz, 115200 8N1 (board debug UART, COM4 through LPC-Link2) |
| Baud setup | Best `OSR`/`BRG` pair computed at start-up |
| Output | Polled TX FIFO, no interrupts, no buffers in RAM |
| Formats | `%c %s %d %i %u %x %X %p %%`, flag `0`, width, `l`/`z`/`h` ignored (32-bit). This covers all MCUboot log formats (`%d %u %x %08x %lx %s`) |
| Before the jump | `sbl_log_deinit()` waits until the TX FIFO is empty and the line is idle, then disables the USART |
| No logging | `CONFIG_MCUBOOT_DISABLE_LOGGING` removes all `BOOT_LOG_*` calls |

### 13.5 New safety check

`rom_auth_init()` stops the boot with:

```text
MCUboot RAM @0x... overlaps ROM reserved RAM 0x20000000-0x20005fff, fix the SRAM memory region
```

It does this if `.data` starts inside the ROM reserved RAM. It also covers the secure alias `0x3000_0000`.

This matters because MCUXpresso IDE once **overwrote `.cproject` with its old in-memory settings** after the file was edited outside the IDE (see 13.6). With this check, a wrong memory layout is reported on the UART instead of hanging inside the ROM call.

### 13.6 Notes

- **Edit `.cproject` only while MCUXpresso IDE is closed**, or make the change in the IDE (Project → Properties → C/C++ Build → MCU settings). Otherwise the IDE may write its old settings back.
- **ECDSA fallback:** turning `CONFIG_BOOT_ROM_AUTHENTICATION` off (the original MCUboot ECDSA P-256 flow) now also needs `mbedtls3x`, `mbedtls_config` and `component/psa_crypto_driver`, `component/rng` back in the build.
- Use the **Release** `.axf` for signing (`Release\lpcxpresso55s69_mcuboot_opensource_cm33_core0.axf`).
- What's left (12 KB): MCUboot core ≈ 4 KB, ROM authentication + port ≈ 2.5 KB, clock/power/flash drivers + startup ≈ 3 KB, TinyCrypt SHA-256 ≈ 1 KB, logger ≈ 0.8 KB. The clock setup (FRO 96 MHz, `fsl_power`) could be removed to save up to about 1.5 KB more, but then MCUboot and the ROM RSA check run at 12 MHz.

### 13.7 Flash and RAM regions (2026-09-30)

The linker regions now match what MCUboot needs, with a margin. The slot layout (`0x20000` / `0x50000`) is **unchanged**, so applications, `imgtool` and the Secure Provisioning configs stay the same.

| Region | Before | After | Used |
|---|---|---|---|
| `PROGRAM_FLASH` (MCUboot) | `0x0`–`0x1FFFF`, 128 KB | **`0x0`–`0x7FFF`, 32 KB** | 12,164 B code (signed MBI 14,700 B) |
| `SRAM` (MCUboot) | `0x2000_6000`–`0x2003_FFFF`, 232 KB | **`0x2000_6000`–`0x2000_9FFF`, 16 KB** | 12,536 B |
| Heap | 32 KB | **0** (no `malloc` since PSA was removed) | – |
| Stack | 8 KB | **4 KB** (`0x2000_9000`–`0x2000_9FFF`, initial SP `0x2000_A000`) | **1,712 B** measured |

- **Flash:** `0x8000`–`0x1FFFF` stays unused. Moving the slots down would give each slot ≈ 299 KB instead of 192 KB, but needs every application re-linked (at `0x8400`) and new `imgtool` / SEC settings. The linker now reports an error if MCUboot grows beyond 32 KB.
- **RAM:** the smaller RAM region doesn't make RAM available to anyone else. After the jump the application owns all RAM, and MCUboot keeps nothing. The benefit is a guard against growth and a clear memory map.
- **Stack measurement:** `SBL_STACK_WATERMARK` (default 1, in `source/main.c`) fills the unused stack with `0xA5A5A5A5` at start-up. Just before the jump it logs:

  ```text
  Stack used: 1712 of 4096 bytes
  ```

  The value includes both `skboot_authenticate()` calls, so the ROM uses the caller's stack. It was the same with an 8 KB stack. The 4 KB stack is 2.4 × the measured use, which leaves room for paths not taken in this boot (update install, ROM rejection). The startup sets `MSPLIM = _vStackBase`, so a stack overflow faults instead of corrupting `.bss`. Set `SBL_STACK_WATERMARK 0` to remove the measurement (≈ 110 B).

Where it's set: `.cproject` memory configuration (`PROGRAM_FLASH` size `0x8000`, `SRAM` size `0x4000`) and heap/stack (`Heap … 0x0`, `Stack … 0x1000`), for both Debug and Release.

Board test (2026-09-30), with the MCUboot region erased as `0x0`–`0x7FFF`: same boot log, ROM authentication passed twice, led_blinky started, `Stack used: 1712 of 4096 bytes`.
