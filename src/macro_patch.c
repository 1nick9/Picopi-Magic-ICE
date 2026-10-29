/*
 * macro_patch.c - "DVD movie: GREEN fix + MACROVISION off"
 *                 MACRO_CHECK_IF_V9to14 .. MODE_SELECT_START
 */
#include "routines.h"
#include "bios_bus.h"
#include "console_io.h"
#include "config.h"
#include "debug_log.h"

/* The asm comments say "send 00,00,00,00" (one MIPS word -> nop).  The SX28
 * did it with back-to-back BIOS_WAIT_OE_LO calls whose exact pulse count
 * depends on /OE pulse width; four bytes is the documented intent. */
#ifndef MACRO_ZERO_BYTES
#define MACRO_ZERO_BYTES 4
#endif
static const uint8_t zeros[MACRO_ZERO_BYTES] = {0};

static void sync_v9_to_v14(void)
{
    /* MACRO_BIOS_PATCH_SYNC_V9toV14: dc 24 10 45 on consecutive reads */
    bios_out_arm(zeros, MACRO_ZERO_BYTES);   /* armed once, fired on match */
    bios_flush();
    bios_sync_fire(BIOS_SEQ4(0xdc, 0x24, 0x10, 0x45), 4);
    bios_out_finish("macrovision v9-v14");
}

static void sync_v1_to_v8(void)
{
    /* 1E 00 63 34, then 24 10, then skip 50 reads, then the zero word.
     * Armed BEFORE the sync: arming takes a few us, and between "24 10" and
     * the end of the 50-strobe skip the ROM keeps streaming (216 ns/byte). */
    bios_out_arm(zeros, MACRO_ZERO_BYTES);
    bios_flush();
    bios_sync(BIOS_SEQ4(0x1e, 0x00, 0x63, 0x34), 4);
    bios_sync(BIOS_SEQ2(0x24, 0x10), 2);
    bios_skip_fire(50);
    bios_out_finish("macrovision v1-v8");
}

ice_st_t __not_in_flash_func(macro_check_if_v9to14)(void)
{
    st.psx_flag = true;

    if (st.bios_rev == '0' ||       /* 2.0  v12  */
        st.bios_rev == '7' ||       /* 1.7  v9-10 */
        st.bios_rev == '9' ||       /* 1.9  v11  */
        st.v14_flag)                /* 'W' region, 75k+ */
    {
        DBG("MACRO_CHECK: v9-v14 sync dc 24 10 45");
        sync_v9_to_v14();
    } else {
        DBG("MACRO_CHECK: v1-v8 sync 1e 00 63 34 / 24 10 / +50");
        sync_v1_to_v8();
    }

    return ST_MODE_SELECT;
}

/*
 * MODE_SELECT_START (entered with reset held through power-on)
 *
 *   MODE_SELECT_TIMER_L1:  call DELAY100m ; snb IO_REST ; jmp HOLD_BOOT_MODES
 *   reset RELEASED within ~1 s          -> HOLD_BOOT_MODES (PS1 mode, psx_flag
 *                                          is still set from the macro patch)
 *   still held after 1 s: wait for release, 0.5 s debounce, then ~10 s:
 *   reset tapped again                  -> DEV1_MODE_LOAD_START
 *   nothing                             -> sleep (disable mode)
 */
ice_st_t mode_select_start(void)
{
    DBG("MODE_SELECT: release reset within 1 s = PS1, keep holding = DEV1/disable");
    for (int i = 0; i < MODE_SELECT_PS1_WINDOW_100MS; i++) {
        delay_100ms();
        if (!reset_is_active()) {
            DBG("MODE_SELECT: reset released -> PS1 mode (HOLD_BOOT_MODES)");
            return ST_HOLD_BOOT_MODES;
        }
    }
    DBG("MODE_SELECT: reset held 1 s, release then tap within 10 s for DEV1");
    while (reset_is_active()) { }                     /* MODE_SELECT_TIMER_L2 */
    for (int i = 0; i < MODE_SELECT_DEBOUNCE_100MS; i++) delay_100ms();
    /* DISABLE_MODE samples IO_REST every 100 ms; a tap shorter than that
     * could fall between two samples.  The RESET falling-edge latch catches
     * any tap in the 10 s window - same result, cannot be missed. */
    ice_edges_clear(RESET_PIN, ICE_EDGE_FALL | ICE_EDGE_RISE);
    for (int i = 0; i < MODE_SELECT_DEV1_WINDOW_100MS; i++) {   /* DISABLE_MODE */
        delay_100ms();
        if (reset_is_active() || (ice_edges(RESET_PIN) & ICE_EDGE_FALL)) {
            DBG("MODE_SELECT: reset tapped -> DEV1 mode");
            return ST_DEV1_MODE_LOAD_START;
        }
    }
    DBG("MODE_SELECT: no reset in 10 s -> disabled until standby");
    return ST_SLEEP_FOREVER;   /* "...sleep chip, can't wake up without stby" */
}
