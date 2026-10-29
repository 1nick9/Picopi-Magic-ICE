/*
 * ps2logo_patch.c - CHECK_IF_START_PS2LOGO / START_PS2LOGO_PATCH_LOAD /
 *                   PS1DRV_PATCHLOAD_v14 / PS2_PS2LOGO:loopz / :back
 */
#include "routines.h"
#include "bios_bus.h"
#include "console_io.h"
#include "patch_tables.h"
#include "config.h"
#include "debug_log.h"
#include "pico/time.h"

ice_st_t ps1drv_patchload_v14(void)
{
    /* 39 bytes of the v14 PS2LOGO image + SET_V14DRV edits + 16 bytes of
     * OSDSYS table[100..115] - all pre-assembled by the extractor */
    sram_load(ps1drv_v14_sram, sizeof ps1drv_v14_sram);
    return ST_TRAY_IS_EJECTED;          /* snb PSX_FLAG ; jmp TRAY_IS_EJECTED */
}

ice_st_t check_if_start_ps2logo(void)
{
    if (st.psx_flag) return ST_TRAY_IS_EJECTED;

    /* START_PS2LOGO_PATCH_LOAD */
    if (st.v14_flag)       sram_load(logo_sram_v14,    sizeof logo_sram_v14);
    else if (st.v10_flag)  sram_load(logo_sram_v10v12, sizeof logo_sram_v10v12);
    else                   sram_load(logo_sram_v1v9,   sizeof logo_sram_v1v9);

    if (st.x_flag) return ST_IS_XCDVDMAN;
    return ST_PS2LOGO_LOOPZ;
}

/*
 * PS2_PS2LOGO:loop4 .. :loop1x_L4 - wait for the 3-byte sync s0 s1 s2 on
 * consecutive ROM reads.  The SX28's timeout counters (loopx) only ran while
 * BIOS CS was high, so the budget is "time the ROM was idle".
 * Returns false on timeout.
 */
static bool logo_sync(uint8_t s0, uint8_t s1, uint8_t s2, int64_t *budget_us)
{
    /* loopx: "sb IO_BIOS_CS ; jmp loop1x" - s0 only while CS is low,
     * s1/s2 on the next strobes (loop1x_L1..L4) */
    return bios_sync_idle(BIOS_SEQ3(s0, s1, s2), 3, BIOS_CE_FIRST, budget_us);
}

ice_st_t __not_in_flash_func(ps2logo_loopz)(void)
{
    int rep = logo_repeat ? logo_repeat : 1;
    int64_t budget = PS2LOGO_SYNC_TIMEOUT_US;

    /* :loopz register setup */
    size_t  len = 117, k = 6;
    uint8_t s0 = 0x01, s1 = 0xc0, s2 = 0x72, preload = 0x00;
    bool    v12_path = false;
    if (!st.v14_flag) {
        len = 103; k = 7; s0 = 0x08; s1 = 0xe0; s2 = 0x9d; preload = 0x40;
        if (st.v12_flag) v12_path = true;
        else {
            s0 = 0xaf; s1 = 0x06; s2 = 0x08; preload = 0x00;
            if (!st.v10_flag) s1 = 0x1e;             /* V1-V9 */
            len = 87; k = 23;                         /* :patchlogo2 */
        }
    }

    for (;;) {
        /* arm BEFORE the sync (arming takes a few us; the 0x0c and the
         * patch follow the sync within a few strobes) */
        if (!v12_path) bios_patch_sram_arm(preload, k, len);
        bios_flush();
        DBG("PS2LOGO: waiting for %02x %02x %02x (rep %u)", s0, s1, s2, rep);
        if (!logo_sync(s0, s1, s2, &budget)) {
            /* AUTORESET: reboot the console into PS1 mode */
            DBG("PS2LOGO: no logo sync in %u ms of BIOS idle -> AUTORESET into PS1 mode",
                (unsigned)(PS2LOGO_SYNC_TIMEOUT_US / 1000));
            console_auto_reset();
            st.psx_flag = true;
            return ST_MACRO_CHECK;
        }

        if (v12_path) {
            /* PS1_MODE_v12_PATCHS: v12 logo goes through OSDSYS sync _P2 */
            st.v12logo_flag = true;
            osdsys_sync_and_patch(preload, k, len, true);
            break;                 /* snb V12LOGO_FLAG ; jmp PS2_PS2LOGO:back */
        } else {
            /* :loop1x_L5 : a 0c byte, then the stream */
            bios_wait_byte_fire(0x0c);             /* writer armed above */
            bios_out_finish("ps2logo");
        }

        if (--rep == 0) break;
        /* decsz VAR_DC4 ; jmp :patchlogo2  (V7: logo patched twice) */
        len = 87;
        if (!st.v14_flag && !st.v12_flag) k = 23;
    }
    return ST_PS2LOGO_BACK;
}

ice_st_t ps2logo_back(void)
{
    if (st.psx_flag) return ST_PS1_LOGO_PATCH;
    st.ej_flag = true;
    return ST_IS_XCDVDMAN;
}

/*
 * PS1_V14_PATCH: the v14 PS1DRV patch rides on the PS2LOGO sync machinery -
 * sync 01 40 1b (entered at ::loop00x), a 0c byte, then 49 bytes from k=6.
 */
ice_st_t __not_in_flash_func(ps1_v14_patch)(void)
{
    int64_t budget = PS2LOGO_SYNC_TIMEOUT_US;
    int rep = logo_repeat ? logo_repeat : 1;

    for (;;) {
        bios_patch_sram_arm(0x00, 6, 49);          /* armed before the sync */
        bios_flush();
        DBG("PS1_V14_PATCH: waiting for 01 40 1b");
        if (!logo_sync(0x01, 0x40, 0x1b, &budget)) {
            DBG("PS1_V14_PATCH: timeout -> AUTORESET");
            console_auto_reset();
            st.psx_flag = true;
            return ST_MACRO_CHECK;
        }
        bios_wait_byte_fire(0x0c);
        bios_out_finish("ps1drv v14");
        if (--rep == 0) break;          /* :patchlogo2 never taken on v14 */
    }
    return ST_PS2LOGO_BACK;             /* psx_flag -> PS1_LOGO_PATCH */
}
