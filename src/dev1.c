/*
 * dev1.c - DEV1_MODE_LOAD_START / FINISHED_RUN_START (modload repatch)
 */
#include "routines.h"
#include "bios_bus.h"
#include "console_io.h"
#include "patch_tables.h"
#include "config.h"
#include "debug_log.h"

ice_st_t dev1_mode_load_start(void)
{
    st.psx_flag  = false;      /* was set by HOLD_BOOT_MODES / macro patch */
    st.soft_rst  = true;
    st.ej_flag   = true;       /* skip logo patch after media for DEVMODE */
    st.dev1_flag = true;
    sram_load(dev1_patch, DEV1_PATCH_LEN);
    DBG("DEV1 mode: %u byte dev1 patch loaded", (unsigned)DEV1_PATCH_LEN);
    return ST_PS2_CHECK_VERSION;          /* PS2_CHECK_IF_V1_v2or3_V4_V5to8 */
}

/*
 * FINISHED_RUN_START: sync C4 27 18, then a D0 followed by FF, then a 42;
 * drive $34 on the next strobe.  Repeated VAR_DC1 times, then DELAY100m and
 * sleep.
 *
 * VAR_DC1 as in the asm: FINISHED_RUN_START loads 4.  The "_P2" entry (from
 * IS_XCDVDMAN after the CS wake-up) does NOT reload it - and DELAY100m used
 * VAR_DC1 as its own counter and left it at 0, so "decsz VAR_DC1" wraps and
 * the SX28 keeps repatching every module load (up to 256) after the first
 * wake-up instead of only 4.
 */
static uint8_t s_dc1;

ice_st_t __not_in_flash_func(finished_run_start)(bool set_intrpt)
{
    static const uint8_t b34[1] = { 0x34 };

    if (set_intrpt) {                     /* FINISHED_RUN_START */
        console_arm_wake_edges();
        s_dc1 = 4;
    }
    unsigned total = s_dc1 ? s_dc1 : 256u, n = 0;

    bios_out_arm(b34, 1);
    bios_flush();                         /* stale bytes from before the entry */
    do {
        n++;
        DBG("DEV1 modload repatch %u/%u: waiting for c4 27 18, d0 ff 42", n, total);
        bios_sync(BIOS_SEQ3(0xc4, 0x27, 0x18), 3);      /* FINISHED_RUN_START_P2 */
        bios_sync_fire(BIOS_SEQ3(0xd0, 0xff, 0x42), 3); /* _P2_L1 .. FINISHED_RUN_END */
        bios_out_finish("dev1 modload");
        bios_out_arm(b34, 1);             /* the next C4 27 18 is a module */
        bios_flush();                     /* load away (100 ms or more)     */
    } while (--s_dc1 != 0);

    delay_100ms();
    s_dc1 = 0;                            /* DELAY100m counted VAR_DC1 down to 0 */
    return ST_SLEEP;
}
