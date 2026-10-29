#include "mech_bus.h"
#include "mech_bus.pio.h"
#include "pio_patch.h"
#include "debug_log.h"
#include "hardware/gpio.h"
#include "bios_bus.h"
#include "pico/time.h"

_Static_assert(MECH_PIO_EDGE_ACTIVE == (MECH_OE_ACTIVE_LOW ? 0 : 1),
               "pio/mech_bus.pio EDGE_ACTIVE must match MECH_OE_ACTIVE_LOW");

static uint          s_out_offset;
static pio_patched_t s_rd_prog, s_out_prog;
#if MECH_RISE_TRACE
static pio_patched_t s_rise_prog;
static uint          s_rise_offset;
volatile uint8_t     mech_rise[MECH_TRACE_LEN];
volatile uint32_t    mech_rise_n;
#endif

void mech_bus_init(void)
{
    PIO pio = MECH_PIO;

    /* edge input (A or /1Q): plain GPIO input, PIO only "waits" on it */
    gpio_init(MECH_EDGE_PIN);
    gpio_set_dir(MECH_EDGE_PIN, GPIO_IN);
    gpio_disable_pulls(MECH_EDGE_PIN);
    for (uint i = 0; i < 4; i++) {                 /* B,G,H,I: PIO bus pins */
        gpio_disable_pulls(MECH_B_PIN + i);
        gpio_set_drive_strength(MECH_B_PIN + i, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_slew_rate(MECH_B_PIN + i, GPIO_SLEW_RATE_FAST);
    }
    /* F: jumper input only, plain GPIO, never driven by the PIO */
    gpio_init(MECH_F_PIN);
    gpio_set_dir(MECH_F_PIN, GPIO_IN);
    gpio_disable_pulls(MECH_F_PIN);
#if H2O75KJMPERS
    /* STARTUP: "mode $0E ; mov !rb,#$be" = SX28 pull-ups ON for rb.0 (F) and
     * rb.6 (H) only.  The jumpers pull these LOW; an open jumper (or a pad
     * that is not wired at all) must read HIGH.  Without the pull-up an
     * unconnected F floats/reads low -> JAP_V8 set -> the v9+ mechacon path
     * (which needs G) runs on a v1-v8 console and never syncs. */
    gpio_pull_up(MECH_F_PIN);
    gpio_pull_up(MECH_H_PIN);
#endif

#if defined(MECH_OE_FLIPFLOP)
    uint rd_off  = pio_add_program(pio,
        pio_patch(&s_rd_prog, &mech_read_ff_program, MECH_EDGE_PIN,
                  mech_read_ff_offset_sample, MECH_SAMPLE_DELAY_CYCLES, PIO_PATCH_NONE));
    s_out_offset = pio_add_program(pio,
        pio_patch(&s_out_prog, &mech_out_ff_program, MECH_EDGE_PIN,
                  PIO_PATCH_NONE, 0, PIO_PATCH_NONE));
    gpio_disable_pulls(MECH_FF_R_PIN);
    mech_read_ff_program_init(pio, MECH_READ_SM, rd_off, MECH_B_PIN, MECH_FF_R_PIN);
    mech_out_program_init(pio, MECH_OUT_SM, s_out_offset, true, MECH_B_PIN);
#else
    /* strobe rule (config.h MECH_GLITCH_FILTER): HC73 = gate0 after the
     * minimum pulse width, late gate a NOP; filter = gate0 a NOP, late gate
     * at the sample point.  Same timing in both. */
    const int m = (int)MECH_MIN_PULSE_CYCLES;
    int rr = (int)MECH_SAMPLE_DELAY_CYCLES - m - 2;      /* fall -> in pins */
    int rw = (int)MECH_WRITE_DELAY_CYCLES  - m - 2;      /* fall -> out pins */
    if (rr < 0) rr = 0;
    if (rw < 0) rw = 0;
    pio_patch(&s_rd_prog, &mech_read_direct_program, MECH_EDGE_PIN,
              mech_read_direct_offset_sample, (uint)m,
              MECH_GLITCH_ACTIVE ? (int)mech_read_direct_offset_gate0
                                 : (int)mech_read_direct_offset_gate);
    pio_patch_delay(&s_rd_prog, mech_read_direct_offset_dly, (uint)rr);
    uint rd_off = pio_add_program(pio, &s_rd_prog.prog);

    pio_patch(&s_out_prog, &mech_out_direct_program, MECH_EDGE_PIN,
              mech_out_direct_offset_sample, (uint)m,
              MECH_GLITCH_ACTIVE ? (int)mech_out_direct_offset_gate0
                                 : (int)mech_out_direct_offset_gate);
    pio_patch_delay(&s_out_prog, mech_out_direct_offset_wdly, (uint)rw);
    s_out_offset = pio_add_program(pio, &s_out_prog.prog);
    mech_read_direct_program_init(pio, MECH_READ_SM, rd_off, MECH_B_PIN);
    mech_out_program_init(pio, MECH_OUT_SM, s_out_offset, false, MECH_B_PIN);
#if MECH_RISE_TRACE
    {
        /* same strobe rule as the reader: gate at min width (HC73) or at the
         * sample point (filter); then the late sample */
        uint gd = MECH_GLITCH_ACTIVE ? MECH_SAMPLE_DELAY_CYCLES : (uint)m;
        const pio_program_t *rp = pio_patch(&s_rise_prog, &mech_rise_program, MECH_EDGE_PIN,
                  mech_rise_offset_sample, gd, PIO_PATCH_NONE);
        /* fall -> late sample = ~3 + gd + 2 + 2*(x+1) cycles */
        int extra = (int)ICE_NS_TO_CYCLES(MECH_READBACK_NS) - (int)gd - 7;
        int x = extra / 2 - 1;
        if (x < 0) x = 0;
        if (x > 31) x = 31;
        s_rise_prog.insn[mech_rise_offset_late] = (uint16_t)pio_encode_set(pio_x, (uint)x);
        s_rise_offset = pio_add_program(pio, rp);
    }
    mech_rise_program_init(pio, MECH_RISE_SM, s_rise_offset, MECH_B_PIN);
#endif
#endif

    pio_sm_set_enabled(pio, MECH_READ_SM, true);
    pio_sm_set_enabled(pio, MECH_OUT_SM, true);

    DBG("mech bus up: B,G,H,I=GP%u-%u edge=GP%u delay=%u cycles, strobe rule %u (0=HC73, 1=filter), F jumper=GP%u",
        MECH_B_PIN, MECH_B_PIN + 3, MECH_EDGE_PIN, MECH_SAMPLE_DELAY_CYCLES,
        MECH_GLITCH_ACTIVE, MECH_F_PIN);
}

volatile uint8_t  mech_trace[MECH_TRACE_LEN];
volatile uint32_t mech_trace_n;          /* total words read by the CPU */

void mech_flush(void)
{
    while (!mech_rx_empty()) (void)MECH_PIO->rxf[MECH_READ_SM];
}

void mech_rise_sync(void)
{
#if MECH_RISE_TRACE
    PIO pio = MECH_PIO;
    /* between strobes: neither SM has a strobe in progress */
    uint32_t t0 = time_us_32();
    while (gpio_get(MECH_EDGE_PIN) == (MECH_OE_ACTIVE_LOW ? 0 : 1)) {
        if (time_us_32() - t0 > 1000) break;
    }
    pio_sm_set_enabled(pio, MECH_RISE_SM, false);
    pio_sm_clear_fifos(pio, MECH_RISE_SM);
    pio_sm_restart(pio, MECH_RISE_SM);
    pio_sm_exec(pio, MECH_RISE_SM, pio_encode_jmp(s_rise_offset));
    mech_flush();
    mech_rise_n = mech_trace_n;
    pio_sm_set_enabled(pio, MECH_RISE_SM, true);
#else
    mech_flush();
#endif
}

/* Jumper reads.  The asm does "clrb IO_CDDVD_OE_A_1R" first: on the original
 * board the jumper goes from F to the flip-flop /1R line, so /1R must be low
 * for a fitted jumper to pull F low.  In FLIPFLOP builds /1R belongs to the
 * read state machine, so drive it low through the SM for the read.  In
 * DIRECT builds there is no /1R: fit the jumper from F (or H) to GND. */
static bool jumper_read(uint pin)
{
#if defined(MECH_OE_FLIPFLOP)
    pio_sm_exec(MECH_PIO, MECH_READ_SM, pio_encode_set(pio_pins, 0));   /* clrb 1R */
    busy_wait_us_32(5);
    bool v = gpio_get(pin);
    pio_sm_exec(MECH_PIO, MECH_READ_SM, pio_encode_set(pio_pins, 1));   /* setb 1R */
    return v;
#else
    return gpio_get(pin);                     /* pull-up enabled at init */
#endif
}

bool mech_pin_f(void) { return jumper_read(MECH_F_PIN); }
/* H is tested in the middle of the mechacon stream ("sb IO_CDDVD_BUS_h"
 * right before the 1-byte patch on the next strobe, ~770 ns away) and the
 * asm does not touch /1R there: plain level read, no delay. */
bool mech_pin_h(void) { return gpio_get(MECH_H_PIN); }

void mech_out(uint8_t data, uint8_t dirs, bool immediate)
{
    pio_sm_put_blocking(MECH_PIO, MECH_OUT_SM, MECH_WORD(data, dirs, immediate));
}

static const uint32_t *s_arm_w;
static size_t          s_arm_n, s_arm_fed;

void mech_out_arm(const uint32_t *w, size_t n)
{
    PIO pio = MECH_PIO;
    pio_sm_set_enabled(pio, MECH_OUT_SM, false);
    pio_sm_clear_fifos(pio, MECH_OUT_SM);
    pio_sm_restart(pio, MECH_OUT_SM);
    pio_sm_exec(pio, MECH_OUT_SM, pio_encode_jmp(s_out_offset));   /* at "pull" */
    s_arm_w = w; s_arm_n = n; s_arm_fed = 0;
    while (s_arm_fed < n && !pio_sm_is_tx_fifo_full(pio, MECH_OUT_SM))
        pio->txf[MECH_OUT_SM] = w[s_arm_fed++];
}

void __not_in_flash_func(mech_out_finish)(size_t edges)
{
    PIO pio = MECH_PIO;
    size_t seen = 0;
    while (s_arm_fed < s_arm_n || seen < edges) {
        if (s_arm_fed < s_arm_n && !pio_sm_is_tx_fifo_full(pio, MECH_OUT_SM))
            pio->txf[MECH_OUT_SM] = s_arm_w[s_arm_fed++];
        if (seen < edges && !mech_rx_empty()) { (void)mech_take(); seen++; }
        else ice_isr_poll();
    }
    mech_out_wait();
}

void mech_out_wait(void)
{
    PIO pio = MECH_PIO;
    while (!pio_sm_is_tx_fifo_empty(pio, MECH_OUT_SM)) { }
    /* last word pulled; wait until the SM is back at `pull block` */
    while (pio_sm_get_pc(pio, MECH_OUT_SM) != s_out_offset) { }
}

/* ---- debug heartbeat ---------------------------------------------------- */
#if ICE_DEBUG
static uint32_t s_hb_last_us, s_hb_last_n;
static const char *s_hb_where = "mech";
static bool        s_hb_ce;

void mech_heartbeat_where(const char *where, bool show_ce)
{
    s_hb_where = where;
    s_hb_ce = show_ce;
    s_hb_last_us = time_us_32();
    s_hb_last_n = mech_trace_n;
    bios_ce_fell_clear();
}

void mech_heartbeat(void)
{
    uint32_t now = time_us_32();
    if (now - s_hb_last_us < 1000000u) return;
    uint32_t n = mech_trace_n;
    uint32_t i = n;                         /* last 8 words, oldest first */
    uint32_t a = 0, b = 0;
    for (int k = 8; k > 0; k--) {
        uint8_t w = mech_trace[(i - k) % MECH_TRACE_LEN];
        if (k > 4) a = (a << 8) | w; else b = (b << 8) | w;
    }
    if (s_hb_ce)
        DBG("  [%s] waiting: %u A strobes/s, BIOS CE went low: %s, last 8 words (bit3..0=I,H,G,B) %08x %08x",
            s_hb_where, (unsigned)(n - s_hb_last_n), bios_ce_fell() ? "yes" : "no",
            (unsigned)a, (unsigned)b);
    else
        DBG("  [%s] waiting: %u A strobes/s, last 8 words (bit3..0=I,H,G,B) %08x %08x",
            s_hb_where, (unsigned)(n - s_hb_last_n), (unsigned)a, (unsigned)b);
    bios_ce_fell_clear();
    s_hb_last_us = now;
    s_hb_last_n = n;
}
#endif
