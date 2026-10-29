/*
 * xcdvdman.c - TRAY_IS_EJECTED / RESUME_MODE_FROM_EJECT / IS_XCDVDMAN(X)
 */
#include "routines.h"
#include "bios_bus.h"
#include "mech_bus.h"
#include "console_io.h"
#include "config.h"
#include "debug_log.h"
#include "hardware/gpio.h"
#include "pico/time.h"

ice_st_t tray_is_ejected(void)
{
    DBG("TRAY_IS_EJECTED: tray %s, waiting for close + BIOS idle",
        tray_is_open() ? "open" : "closed");
    uint32_t hb_t = time_us_32(), ce_restarts = 0, tray_restarts = 0;
#define IDLE_WAIT_REPORT()                                                        \
    do { if (ICE_DEBUG && time_us_32() - hb_t >= 1000000u) {                      \
        DBG("  [BIOS idle wait] tray %s, restarts in last 1 s: BIOS CE low %u, tray %u", \
            tray_is_open() ? "OPEN" : "closed", (unsigned)ce_restarts,           \
            (unsigned)tray_restarts);                                             \
        hb_t = time_us_32(); ce_restarts = tray_restarts = 0; } } while (0)
    for (;;) {
        IDLE_WAIT_REPORT();
        /* TRAY_IS_EJECTED */
        if (reset_is_active()) { DBG("TRAY_IS_EJECTED: reset -> TAP_BOOT_MODE"); return ST_TAP_BOOT_MODE; }
        if (tray_is_open()) continue;          /* wait for tray closed */

        /* RESUME_MODE_FROM_EJECT: 5 x 100 ms of BIOS CE inactivity, watching
         * reset and eject on the way.  CE low pulses are 210-870 ns and come
         * back-to-back while the ROM is busy (gaps 20-30 ns), so level
         * polling every few ns sees them; idle stretches are >= 1 s. */
        bool restart = false;
        absolute_time_t until = make_timeout_time_ms(500);
        while (!time_reached(until)) {
            if (bios_ce_low())        { restart = true; ce_restarts++; break; }
            if (reset_is_active())    { DBG("RESUME: reset -> TAP_BOOT_MODE"); return ST_TAP_BOOT_MODE; }
            if (tray_is_open())       { restart = true; tray_restarts++; break; }
            IDLE_WAIT_REPORT();
        }
        if (restart) continue;
        break;
    }
    DBG("tray closed, BIOS idle 500 ms -> resume (dev1=%u)", st.dev1_flag);

    console_arm_wake_edges();                  /* call SET_INTRPT */

    if (st.dev1_flag) return ST_START_CDDVD_PATCH;

    logo_repeat = (st.bios_yr == '2') ? 2 : 1; /* 2 logo patches for V7 */

    /* CONSOLE_2002_JMP */
#if H2O75KJMPERS
    /* sb IO_CDDVD_BUS_f ; setb JAP_V8 : F pulled LOW by the jumper = v8 JAP
     * (last SPC mechacon).  Open / unwired F reads high (pull-up). */
    if (!mech_pin_f()) st.jap_v8 = true;
    DBG("CONSOLE_2002_JMP: F jumper reads %s -> jap_v8=%u",
        st.jap_v8 ? "LOW (fitted)" : "HIGH (open)", st.jap_v8);
#endif
#if defined(JAPv14orv8)
    st.jap_v8 = true;
#endif
    return ST_START_CDDVD_PATCH;
}

ice_st_t __not_in_flash_func(is_xcdvdman)(bool set_intrpt)
{
    if (set_intrpt) console_arm_wake_edges();  /* IS_XCDVDMANX */

    for (;;) {                                 /* IS_XCDVDMAN */
        if (st.dev1_flag) return ST_FINISHED_RUN_START_P2;

        /* VAR_DC4..DC1 = 200 x 255^3 passes of loopx, 120 ns each, and loopx
         * only counts while BIOS CS is high (a CS-low strobe goes to the
         * compare instead): a budget of ROM-idle time, reset on every entry */
        int64_t idle_budget_us = (int64_t)XCDVDMAN_RELOAD_TIMEOUT_MS * 1000;
        /* armed BEFORE the sync: after 27 18 00 A3 the asm patches on the next
         * 27, a few strobes later - arming there (a few us) would lose those
         * bytes and could patch a later 27 */
        bios_patch_sram_arm(0x08, 0, 7);
        bios_flush();
        DBG("IS_XCDVDMAN: waiting for a2 93 34 (x_flag=%u ej_flag=%u)",
            st.x_flag, st.ej_flag);

        /* sync A2 93 34 (loopx / loop0 / loop1): "sb IO_BIOS_CS ; jmp loop1"
         * - the first byte only counts if CS was low, the next two are plain
         * BIOS_WAIT_OE_LO strobes. */
        if (!bios_sync_idle(BIOS_SEQ3(0xa2, 0x93, 0x34), 3, BIOS_CE_FIRST, &idle_budget_us)) {
            DBG("IS_XCDVDMAN: no reload within %u s of BIOS-idle time -> sleep",
                (unsigned)(XCDVDMAN_RELOAD_TIMEOUT_MS / 1000));
            return ST_SLEEP;
        }

        /* XCDVDMAN: 27 18 00 A3 */
        bios_sync(BIOS_SEQ4(0x27, 0x18, 0x00, 0xa3), 4);

        if (st.x_flag) {                       /* first XMAN executed: skip */
            DBG("IS_XCDVDMAN: first cdvdman load seen, patching the next one");
            st.x_flag = false;
            continue;
        }

        /* patch export 0x23 (CdCheck): 08 00 E0 03 21 10 00 00 after a 27
         * (xcdvdman1_l1 .. RUN_BIOS_PATCHES_SRAM), writer already armed */
        bios_wait_byte_fire(0x27);
        bios_out_finish("xcdvdman CdCheck");

        if (!st.ej_flag) return ST_TRAY_IS_EJECTED;   /* first xman for originals */
        /* else loop: patch cddvdman & xcdvdman again ("da verificare") */
    }
}
