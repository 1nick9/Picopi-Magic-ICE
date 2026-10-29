/*
 * ps1_patch.c - PS1_MODE_START_PATCH / PS1_SCEX_INJECT_COMPLETE /
 *               PS1_CONSOLE_PAL_YFIX / PS1_LOGO_PATCH
 */
#include "routines.h"
#include "bios_bus.h"
#include "scex.h"
#include "config.h"
#include "debug_log.h"

ice_st_t ps1_mode_start_patch(void)
{
    st.scex_flag = false;
    uint8_t tries = 0xff;        /* "max value helps bad optics ;)" */
    DBG("PS1_MODE_START_PATCH: sending SCEx (%s)",
        st.jap_flag ? "SCEI" : (st.uk_flag ? "SCEE" : "SCEA"));

    uint32_t bursts = 0;
    for (;;) {
        /* RUN_PS1_SCEX_INJECT */
        bool was_set = st.scex_flag;
        scex_send();
        bursts++;
        if (!st.scex_flag) {
            if (--tries == 0) { DBG("SCEx: 255 bursts, CS never low in SCEX_LOW -> sleep"); return ST_SLEEP; }
            continue;
        }
        /* SCEX_FLAG is only cleared in PS1_MODE_START_PATCH (above), so once
         * set it stays set - as in the asm, later bursts do not re-test it */
        if (!was_set)
            DBG("SCEx: CS seen low in SCEX_LOW after %u burst(s) -> SCEX_FLAG set%s",
                (unsigned)bursts, st.ej_flag ? " (stays set)" : "");
        /* PS1_SCEX_INJECT_COMPLETE */
        if (st.ej_flag) {           /* "snb EJ_FLAG ; jmp RUN_PS1_SCEX_INJECT": */
            if (bursts == 1)        /* bios patched -> SCEx for ever (until power off) */
                DBG("SCEx: BIOS patched (EJ_FLAG) -> continuous SCEx as in the asm, "
                    "progress every 40 bursts (~10 s)");
            else if (bursts % 40 == 0)
                DBG("SCEx: %u bursts sent", (unsigned)bursts);
            continue;
        }
        break;
    }

    logo_repeat = (st.bios_yr == '2') ? 2 : 1;      /* PS1DRV patched twice on V7 */

    /* PS1_IS_V14orV1toV12PALorNTSC */
    if (st.v14_flag) return ps1_v14_patch();
    if (st.uk_flag)  return ST_PS1_CONSOLE_PAL_YFIX;
    return ST_PS1_LOGO_PATCH;        /* NTSC console: no Y-fix */
}

/* PS1_CONSOLE_PAL_YFIX: 11 11 00 09, then a 30, then $3c + ps1drv_data */
ice_st_t __not_in_flash_func(ps1_console_pal_yfix)(void)
{
    int rep = logo_repeat ? logo_repeat : 1;
    while (rep--) {
        bios_patch_sram_arm(0x3c, 0, 11);
        bios_flush();
        DBG("PS1 PAL Y-fix: waiting for 11 11 00 09, 30");
        bios_sync(BIOS_SEQ4(0x11, 0x11, 0x00, 0x09), 4);
        bios_wait_byte_fire(0x30);                  /* _SYNC_L1 */
        bios_out_finish("ps1 pal yfix");
    }
    return ST_PS1_LOGO_PATCH;
}

/* PS1_LOGO_PATCH: match FD FF 85 14 (strobe S), then in the asm:
 *   skip 52 (S+1..S+52)            -> 03 80 04 3C on S+53..S+56
 *   release after S+56 rises, skip 24 (S+57..S+80)
 *                                  -> 00 00 00 00 on S+81..S+84
 *   release after S+84 rises, skip 24 (S+85..S+108)
 *                                  -> 88 02 80 A4 on S+109..S+112
 * Each part is one 32-bit MIPS instruction (lui a0,0x8003 / nop /
 * sh zero,0x288(a0)), so the parts must start 28 bytes apart on the same
 * word alignment as S+1 (FD FF 85 14 is the word before); a part on any
 * other strobe straddles two instructions and corrupts the PS1 BIOS code.
 * A cycle model of the asm (20 ns SX cycles against 216 ns ROM strobes)
 * gives S+53 / S+81 / S+109 in the large majority of phases.
 * One armed writer stream, so the CPU is never in the loop between parts. */
ice_st_t __not_in_flash_func(ps1_logo_patch)(void)
{
    static const uint8_t p1[4] = { 0x03, 0x80, 0x04, 0x3c };
    static const uint8_t p2[4] = { 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t p3[4] = { 0x88, 0x02, 0x80, 0xa4 };
    static const bios_seg_t segs[] = {
        { p1, 4 }, { NULL, 24 }, { p2, 4 }, { NULL, 24 }, { p3, 4 },
    };

    bios_out_arm_segs(segs, sizeof segs / sizeof segs[0]);
    bios_flush();
    DBG("PS1_LOGO_PATCH: waiting for fd ff 85 14");
    bios_sync(BIOS_SEQ4(0xfd, 0xff, 0x85, 0x14), 4);
    bios_skip_fire(52);
    bios_out_finish("ps1 logo (3 parts, +53/+81/+109)");

    st.ej_flag = true;
    return ST_PS1_MODE_START_PATCH;
}
