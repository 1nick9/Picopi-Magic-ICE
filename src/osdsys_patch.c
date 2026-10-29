/*
 * osdsys_patch.c - PS2_MODE_START / PS2_CHECK_IF_V1_v2or3_V4_V5to8 /
 *                  OSDSYS_BIOS_PATCH_SYNC / LOAD_PATCH_DEV1_STACK
 */
#include "routines.h"
#include "bios_bus.h"
#include "patch_tables.h"
#include "config.h"
#include "debug_log.h"

/*
 * OSDSYS_BIOS_PATCH_SYNC: 60 00 04 08 (consecutive), then a 07 anywhere,
 * then 03 immediately followed by 24.  The armed stream goes out on the
 * pulses right after the 24.  Returns after the bus is released.
 */
void __not_in_flash_func(osdsys_sync_and_patch)(uint8_t preload, size_t k, size_t n,
                                                bool from_p2)
{
    bios_patch_sram_arm(preload, k, n);
    bios_flush();

    if (!from_p2) {                                /* OSDSYS_BIOS_PATCH_SYNC */
        DBG("OSDSYS: waiting for 60 00 04 08, 07, 03 24 (%u bytes)", (unsigned)n + 1);
        bios_sync(BIOS_SEQ4(0x60, 0x00, 0x04, 0x08), 4);
    }
    bios_wait_byte(0x07);                          /* _P2 */
    bios_sync_fire(BIOS_SEQ2(0x03, 0x24), 2);      /* _P3 .. _P4_L1, then
                                                    * _P4_L2/_L3 + RUN_SRAM */
    bios_out_finish(from_p2 ? "v12 logo (osdsys _P2)" : "osdsys");
}

ice_st_t ps2_mode_start(bool skip_psx_check)
{
    if (!skip_psx_check && st.psx_flag) {
        /* PSX mode: only cache the PS1DRV data, patched later */
        if (st.v14_flag) return ps1drv_patchload_v14();
        DBG("PS2_MODE_START: PS1 mode, PS1DRV data cached");
        sram_load(ps1drv_data, sizeof ps1drv_data);     /* w=#$59, 11 bytes */
        return ST_TRAY_IS_EJECTED;
    }

    /* PS2_CHECK_IF_V1_v2or3_V4_V5to8 / CHECK_V9to14_REV */
    const uint8_t *osd; size_t osd_len;
    uint8_t dc3, dc4;                 /* only consumed by the DEV1 patch */

    if (!st.v14_flag && st.bios_rev == '1') {          /* V1_CONSOLE_11_BIOS */
        osd = osd_v1;   osd_len = sizeof osd_v1;   dc3 = 176; dc4 = 116;
    } else if (!st.v14_flag && st.bios_rev == '2') {   /* V2or3_CONSOLE_12_BIOS */
        osd = osd_v2v3; osd_len = sizeof osd_v2v3; dc3 = 64;  dc4 = 122;
    } else if (!st.v14_flag && st.bios_rev == '5') {   /* V4_CONSOLE_15_BIOS */
        osd = osd_v4;   osd_len = sizeof osd_v4;   dc3 = 96;  dc4 = 125;
    } else if (st.bios_rev == '7') {                   /* V9_CONSOLE_17_BIOS */
        osd = osd_v9;   osd_len = sizeof osd_v9;   dc3 = 4;   dc4 = 148;
    } else if (st.bios_rev == '9') {                   /* V9_CONSOLE_19_BIOS (v10/11) */
        st.v10_flag = true;
        osd = osd_v10;  osd_len = sizeof osd_v10;  dc3 = 100; dc4 = 158;
    } else if (st.bios_rev == '0') {                   /* V12_CONSOLE_20_BIOS */
        st.v10_flag = true; st.v12_flag = true;
        osd = osd_v12;  osd_len = sizeof osd_v12;  dc3 = 124; dc4 = 169;
    } else if (st.bios_rev == '2') {                   /* V14_CONSOLE_22_BIOS */
        st.v10_flag = true; st.v14_flag = true;
        osd = osd_v14;  osd_len = sizeof osd_v14;  dc3 = 212; dc4 = 169;
    } else {                                           /* V5-V8 */
        osd = osd_v5v8; osd_len = sizeof osd_v5v8; dc3 = 48;  dc4 = 125;
    }

    DBG("PS2_MODE_START: rev '%c' -> osd table %u bytes, dev1=%u",
        DBG_CH(st.bios_rev), (unsigned)osd_len, st.dev1_flag);

    /* ALL_CONTIUNE_BIOS_PATCH */
    if (st.dev1_flag) {
        /* LOAD_PATCH_DEV1_STACK: dev1 image is already in SRAM; drop the
         * per-console pair into bytes 7,8, preload $04 */
        sram[7] = dc3;
        sram[8] = dc4;
        osdsys_sync_and_patch(0x04, 0, DEV1_PATCH_LEN, false);
        return ST_FINISHED_RUN_START;
    }

    /* LOAD_OSDSYS_BIOS_PATCH_DATA: osd[0] is the preload, rest is SRAM */
    sram_load(osd + 1, osd_len - 1);
    if (st.psx_flag) return ST_TRAY_IS_EJECTED;        /* (dev1 entry only) */

    osdsys_sync_and_patch(osd[0], 0, osd_len - 1, false);

    /* (v12logo_flag was cleared in HOLD_BOOT_MODES; the V12 logo path enters
     *  the sync at _P2 from ps2logo_loopz instead) */
    return ST_CHECK_IF_START_PS2LOGO;
}
