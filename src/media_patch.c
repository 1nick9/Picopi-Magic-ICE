/*
 * media_patch.c - START_CDDVD_PATCH .. CDDVD_IS_PS1  (mechacon bus)
 *
 * Every "snb IO_CDDVD_OE_A_1Q / clrb 1R / nop / setb 1R" edge wait of the
 * asm is one mech_read().  Writes go through the PIO writer, which applies a
 * nibble at the next OE edge exactly where the asm did `mov IO_CDDVD_BUS,w`
 * right after detecting the edge.
 */
#include "routines.h"
#include "mech_bus.h"
#include "console_io.h"
#include "patch_tables.h"
#include "config.h"
#include "debug_log.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#define IB   (MECH_I | MECH_B)
#define IGB  (MECH_I | MECH_G | MECH_B)
#define IG   (MECH_I | MECH_G)

/* V1toV8_CONSOLE_CDDVD_START: 4 consecutive edges with I=1,B=1 */
static void v1_to_v8_sync(void)
{
    mech_heartbeat_where("v1-v8 sync 4x I,B=1", false);
    for (;;) {
        int n = 4;
        while (n && (mech_read() & IB) == IB) n--;
        if (n == 0) return;
    }
}

/* V9toV14_CONSOLE_CDDVD_START .. V9toV12_AND_BYTE_SYNC2
 * returns false if the asm would sleep (DVD media in PSX mode) */
static bool v9_to_v14_sync_and_first_patch(void)
{
    mech_heartbeat_where("v9-v14 sync FA-FC/FF", false);
    for (;;) {
        /* SYNC1: wait for I=1,G=1,B=0 ("FA-FC") */
        while ((mech_read() & IGB) != IG) { }
        /* L2: next must be all ones ("FF") or all zero ("00") */
        uint8_t v = mech_read() & IGB;
        if (v != IGB && v != 0) continue;
        /* SYNC2 */
        v = mech_read() & IGB;
        if (v == IGB) return true;            /* "FF" -> PATCH1_POST */
        if (v != IG) continue;                /* not "FC" -> restart   */

        if (st.psx_flag) return false;        /* PS2_MODE_RB_IO_SET_SLEEP */

        /* 15 edges, then drive 0 on G,H,I from the next edge for 5 more
         * edges, then release ("patch bus first time", DVD9 / DVD-RW) */
        mech_wait_oe(15);
        const uint32_t w[2] = {
            MECH_WORD(0, MECH_NIB_G | MECH_NIB_H | MECH_NIB_I, false),   /* byte #1 */
            MECH_WORD(0, 0, true),                                       /* after 5 */
        };
        /* word 0 applies at edge E16; the asm then counted 5 edges and
         * released at once - queue the release after we have seen them */
        pio_sm_put_blocking(MECH_PIO, MECH_OUT_SM, w[0]);
        mech_wait_oe(1 + 5);
        pio_sm_put_blocking(MECH_PIO, MECH_OUT_SM, w[1]);
        mech_out_wait();
        return true;
    }
}

/* one edge of ALL_CDDVD_PATCH1_GET_SYNC_BIT, with the BIOS-CS exit:
 *     sb IO_BIOS_CS ; jmp CDDVD_IS_PS1
 * leaves when CS is LOW (the BIOS is being read again = the disc is booting).
 * CS HIGH is the idle state while the drive spins up. */
typedef enum { EDGE_OK, EDGE_FAIL, EDGE_CS_LOW } edge_r;

/* ---- debug read-back of the mechacon patches ----------------------------
 * For each strobe around a patch: the bus at the sample point (what the asm
 * would read), the bus late in the strobe (what the mechacon latches), what
 * the patch wanted and which bits it drove.  One hex digit per strobe,
 * bit3..0 = I,H,G,B, 4 strobes per line.  "A-late" = MECH_READBACK_NS after
 * A fell, A still low: the Pico and the mechacon-side driver both on the bus.
 * If (A-late & drive) != (want & drive)
 * on a driven strobe, the Pico lost the bus to the mechacon side (drive
 * strength / series resistance) or wrote on the wrong strobe. */
#if MECH_RISE_TRACE
#define RB_MAX 52
static uint8_t  s_rb_want[RB_MAX], s_rb_drv[RB_MAX];
static uint32_t s_rb_first, s_rb_n;
static bool     s_rb_pending;

static void rb_dump(const char *what, uint32_t first, uint32_t n)
{
    for (uint32_t o = 0; o < n; o += 4) {
        uint32_t sv = 0, rv = 0, wv = 0, dv = 0;
        for (uint32_t k = o; k < o + 4; k++) {
            uint32_t i = first + k;
            bool ok = k < n;
            sv = (sv << 4) | (ok ? mech_trace[i % MECH_TRACE_LEN] : 0);
            rv = (rv << 4) | (ok ? mech_rise[i % MECH_TRACE_LEN] : 0);
            wv = (wv << 4) | (ok ? s_rb_want[k] : 0);
            dv = (dv << 4) | (ok ? s_rb_drv[k] : 0);
        }
        DBG("  %s +%02u: sample %04x  A-late %04x  want %04x  drive %04x",
            what, (unsigned)o, (unsigned)sv, (unsigned)rv, (unsigned)wv, (unsigned)dv);
    }
}

static void rb_wait_rise(uint32_t upto)
{
    uint32_t t0 = time_us_32();
    while ((int32_t)(mech_rise_n - upto) < 0 && time_us_32() - t0 < 20) mech_rise_poll();
}

static void rb_region_check(bool force)
{
    if (!s_rb_pending) return;
    uint32_t need = s_rb_first + s_rb_n;
    if (!force && ((int32_t)(mech_trace_n - need) < 0 || (int32_t)(mech_rise_n - need) < 0))
        return;
    s_rb_pending = false;
    uint32_t n = s_rb_n;
    if (force) { uint32_t have = mech_trace_n - s_rb_first; if (have < n) n = have; }
    DBG("mech: region patch read-back, strobes j..j+%u (j+0..8 = 3x FA FF FF, "
        "j+9..24 = nibbles, j+25 = release)", (unsigned)(n - 1));
    rb_dump("region", s_rb_first, n);
}
#else
static inline void rb_region_check(bool force) { (void)force; }
#endif

/* CDDVD_PATCH_POST_RB_INPUT does not reset /1R before "jmp
 * ALL_CDDVD_PATCH1_GET_SYNC_BIT" (SOFT_RST repeat), so /1Q is still low at the
 * top and the first FA test reads the bus of that last patch edge again
 * instead of waiting for a new one.  s_replay holds that edge. */
static int s_replay = -1;

static edge_r sync_edge(bool want_i, bool want_b, bool check_cs)
{
    uint8_t v;
    for (;;) {
        if (check_cs && !gpio_get(BIOS_CE_PIN)) return EDGE_CS_LOW;
        if (s_replay >= 0) { v = (uint8_t)s_replay; s_replay = -1; break; }
        if (!mech_rx_empty()) { v = mech_take(); rb_region_check(false); break; }
        mech_rise_poll();
        ice_isr_poll();
        mech_heartbeat();
    }
    bool i = (v & MECH_I) != 0, b = (v & MECH_B) != 0;
    return (i == want_i && b == want_b) ? EDGE_OK : EDGE_FAIL;
}

ice_st_t __not_in_flash_func(start_cddvd_patch)(void)
{
    mech_rise_sync();                /* = mech_flush + read-back in step */
    s_replay = -1;
    /* writer idle and running (an armed-but-unfired stream from a previous
     * pass, left when the CS exit was taken, is dropped here) */
    mech_out_arm(NULL, 0);
    mech_out_fire();

#if !MECH_PATCH
    /* test build: never drive the mechacon bus.  Only the BIOS patches run;
     * wait for the BIOS CE low exit as the region loop would. */
    DBG("START_CDDVD_PATCH: MECH_PATCH=OFF - mechacon bus left alone, waiting for BIOS CE low");
    mech_heartbeat_where("no mech patch, waiting for BIOS CE low", true);
    while (gpio_get(BIOS_CE_PIN)) {
        if (!mech_rx_empty()) (void)mech_take();
        mech_heartbeat();
    }
    goto is_ps1;
#endif

    /* which mechacon generation? */
    bool v9plus = st.v14_flag || st.jap_v8 || st.bios_rev == '0' || st.bios_rev >= '7';
    DBG("START_CDDVD_PATCH: %s mechacon path (rev '%c' v14=%u jap_v8=%u), psx=%u",
        v9plus ? "v9-v14" : "v1-v8", DBG_CH(st.bios_rev), st.v14_flag, st.jap_v8,
        st.psx_flag);
    if (!v9plus) {
        v1_to_v8_sync();
        DBG("mech: v1-v8 sync done");
    } else {
        if (!v9_to_v14_sync_and_first_patch()) {
            DBG("mech: DVD media in PS1 mode -> sleep");
            return ST_SLEEP;
        }
        DBG("mech: v9-v14 sync done");
        /* V9toV12_CONSOLE_PATCH1_POST */
        if (st.psx_flag) return ST_PS1_MODE_START_PATCH;
    }

    /* the asm's only CS test in the mechacon code: top of
     * ALL_CDDVD_PATCH1_GET_SYNC_BIT ("sb IO_BIOS_CS ; jmp CDDVD_IS_PS1") */
    mech_heartbeat_where("region sync FA FF FF 01 00 00 01 / CS-low exit (asm)", true);

    /* 1-byte patch, armed before the sync, fired on the match.  The asm:
     *     mov w,#$90 ; mov IO_CDDVD_BUS,w ; mov w,#$6f
     *   ALL_CDDVD_PATCH1:  snb /1Q ; jmp ...        ; edge k+1
     *                      clrb /1R                 ; flip-flop reset
     *                      mov !IO_CDDVD_BUS,w      ; drive I,B = 1
     *                      setb /1R
     *   CDDVD_REGION:      snb /1Q ; jmp ...        ; waits for edge k+2
     *                      clrb /1R ; mov w,#$ff ; mov !IO_CDDVD_BUS,w ; setb /1R
     *   ALL_CDDVD_PATCH_SYNC2_BIT_L1: first FA candidate = edge k+3
     * I,B are driven from edge k+1 to edge k+2, so the mechacon reads them at
     * the end (A rising) of strobe k+1; SYNC2 starts on k+3. */
    static const uint32_t w1[2] = {
        MECH_WORD(MECH_NIB_I | MECH_NIB_B, MECH_NIB_I | MECH_NIB_B, false), /* edge k+1 */
        /* release keeps the driven level in the pin latch: the writer does
         * "out pins" before "out pindirs", so data 0 here pulled I,B low
         * for one PIO cycle (~4 ns) before letting go */
        MECH_WORD(MECH_NIB_I | MECH_NIB_B, 0, false),                       /* edge k+2 */
    };
    static uint32_t wr[1 + 16 + 1];                /* region patch words */

    for (;;) {
        /* ALL_CDDVD_PATCH1_GET_SYNC_BIT: FA FF FF 01 00 00 01 by I/B bits.
         * L4-L6 failures go via PATCH1_POST (psx check), the rest to top. */
        edge_r r;
        mech_out_arm(w1, 2);
      top:
        r = sync_edge(1, 0, true);                       /* FA */
        if (r == EDGE_CS_LOW) goto is_ps1;
        if (r != EDGE_OK) goto top;
        if (sync_edge(1, 1, false) != EDGE_OK) goto top; /* FF   (L1) */
        if (sync_edge(1, 1, false) != EDGE_OK) goto top; /* FF   (L2) */
        if (sync_edge(0, 0, false) != EDGE_OK) goto top; /* 01   (L3) */
        if (sync_edge(0, 1, false) != EDGE_OK) goto post; /* L4 */
        if (sync_edge(0, 1, false) != EDGE_OK) goto post; /* L5 */
        if (sync_edge(0, 0, false) != EDGE_OK) goto post; /* L6 */

#if H2O75KJMPERS
        if (!mech_pin_h()) st.jap_flag = true;   /* JAP v14 jumper on H */
#endif
        if (st.psx_flag) return ST_SLEEP;        /* V1-V8: DVD media in PSX mode */

        /* "NEW 1 time 1 BYTE patch": drive I=1,B=1 at the next edge, release
         * at the one after (ALL_CDDVD_PATCH1 / CDDVD_REGION) */
        s_replay = -1;
        mech_out_fire();
        mech_out_finish(2);                 /* k+1: drive I,B   k+2: release */
#if MECH_RISE_TRACE
        {   /* strobes k-6..k = FA FF FF 01 00 00 01, k+1 patched, k+2 released */
            uint32_t first = mech_trace_n - 9;
            for (int o = 0; o < 9; o++) { s_rb_want[o] = 0; s_rb_drv[o] = 0; }
            s_rb_want[7] = MECH_I | MECH_B; s_rb_drv[7] = MECH_I | MECH_B;
            rb_wait_rise(mech_trace_n);
            DBG("mech: 1-byte patch read-back, strobes k-6..k+2 (k-6..k = FA FF FF 01 00 00 01, "
                "k+1 = I,B driven, k+2 = release)");
            rb_dump("1byte ", first, 9);
        }
#endif

        /* region table offset; build + arm the region words BEFORE SYNC2 */
        int off = st.jap_flag ? 16 : (st.uk_flag ? 8 : 0);
        {
            size_t k = 0;
            wr[k++] = MECH_WORD(0xf, 0xf, true);         /* !rb=$0f with rb=$ff latched */
            for (int i = 0; i < 8; i++) {
                uint8_t b = cddvd_patch_data[off + i];   /* high nibble first (rb4-7), */
                wr[k++] = MECH_WORD(b >> 4,  0xf, false); /* then swapped low nibble   */
                wr[k++] = MECH_WORD(b & 0xf, 0xf, false);
            }
            /* CDDVD_PATCH_POST_RB_INPUT: release with the last nibble kept in
             * the latch (no ~4 ns 0000 blip before the pins float) */
            wr[k++] = MECH_WORD(cddvd_patch_data[off + 7] & 0xf, 0, false);
            mech_out_arm(wr, k);
        }
        DBG("mech: region sync found, 1-byte patch sent (jap=%u uk=%u), region words armed",
            st.jap_flag, st.uk_flag);

        /* ALL_CDDVD_PATCH_SYNC2_BIT: 3 x (FA FF FF) */
        for (;;) {
            int n = 3;
            while (n) {
                if (sync_edge(1, 0, false) != EDGE_OK) break;
                if (sync_edge(1, 1, false) != EDGE_OK) break;
                if (sync_edge(1, 1, false) != EDGE_OK) break;
                n--;
            }
            if (n == 0) break;
        }

        /* patch region: B,G,H,I outputs now ($ff latched), then 8 bytes as
         * high nibble / low nibble on the next 16 edges, release at edge 17 */
        mech_out_fire();
        mech_out_finish(17);
        DBG("mech: region patch sent (8 bytes from table[%u]), soft_rst=%u", off, st.soft_rst);
#if MECH_RISE_TRACE
        {   /* j..j+8 = SYNC2 (j+8: $0f immediate, B,G,H,I driven 1), j+9..j+24
             * nibbles, j+25 release, then 24 strobes of the drive's answer */
            s_rb_n = 9 + 17 + 24;
            s_rb_first = mech_trace_n - 17 - 9;
            for (uint32_t o = 0; o < s_rb_n; o++) { s_rb_want[o] = 0; s_rb_drv[o] = 0; }
            s_rb_want[8] = 0xf; s_rb_drv[8] = 0xf;
            for (int i = 0; i < 8; i++) {
                uint8_t b = cddvd_patch_data[off + i];
                s_rb_want[9 + 2 * i] = b >> 4;  s_rb_drv[9 + 2 * i] = 0xf;
                s_rb_want[10 + 2 * i] = b & 0xf; s_rb_drv[10 + 2 * i] = 0xf;
            }
            s_rb_pending = true;
        }
#endif

        if (st.soft_rst) {                         /* another disk patch */
            s_replay = mech_trace[(mech_trace_n - 1) % MECH_TRACE_LEN];
            continue;
        }
        goto is_ps1;

      post:
        if (st.psx_flag) return ST_PS1_MODE_START_PATCH;
        goto top;
    }

  is_ps1:
    rb_region_check(true);
    /* CDDVD_IS_PS1 */
    DBG("mech: BIOS CE low -> disc booting (CDDVD_IS_PS1), ej_flag=%u", st.ej_flag);
    st.soft_rst = false;
    if (st.ej_flag) return ST_IS_XCDVDMAN;
    return ST_PS2LOGO_LOOPZ;
}
