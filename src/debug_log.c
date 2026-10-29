/*
 * debug_log.c - USB debug output (only built into ICE_DEBUG builds).
 *
 * core 1 : the modchip (ice_main) - pushes events with dbg_event()
 * core 0 : stdio_usb + this printer loop
 *
 * Serial commands (type in the terminal):
 *   c  print the configuration banner again
 *   p  print the current level of every bus pin
 *   m  dump the last 64 mechacon words the modchip read (F,B,G,H,I)
 *   e  measure BIOS CE / OE and mechacon A as the Pico sees them (compare
 *      with a logic analyser: shows input delay from series resistors)
 *   r  reboot the Pico (re-run from "power on" without unplugging)
 */
#include "debug_log.h"
#include "console_io.h"

#if ICE_DEBUG

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "routines.h"
#include "mech_bus.h"

#ifndef ICE_DEBUG_QUEUE_LEN
#define ICE_DEBUG_QUEUE_LEN 256      /* events kept while no terminal is open */
#endif

/* When the Pico is powered from USB it does not lose power when the PS2
 * goes to standby (the SX28 did, and restarted from power-on).  If the
 * console RESET pad stays low this long, treat it as "console off" and
 * reboot so the next power-on is caught from the start.  0 = disabled. */
#ifndef ICE_DEBUG_STANDBY_REBOOT_MS
#define ICE_DEBUG_STANDBY_REBOOT_MS ICE_STANDBY_MS
#endif

typedef struct {
    const char *fmt;                 /* NULL = state transition, a = state */
    uint32_t    a, b, c, d, e, f;
    uint32_t    t_us;
} dbg_ev_t;

static queue_t           s_q;
static volatile uint32_t s_dropped;
static uint32_t          s_core1_stack[8192 / 4];

void dbg_event(const char *fmt, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
               uint32_t e, uint32_t f)
{
    dbg_ev_t ev = { fmt, a, b, c, d, e, f, time_us_32() };
    if (!queue_try_add(&s_q, &ev)) s_dropped++;
}

void dbg_state(int state)
{
    dbg_event(NULL, (uint32_t)state, 0, 0, 0, 0, 0);
}

static const char *mech_mode_name(void)
{
#if defined(MECH_OE_FLIPFLOP)
    return "FLIPFLOP (/1Q, /1R)";
#else
    return "DIRECT (A)";
#endif
}

static void print_banner(void)
{
    printf("\n==== ps2-ice-pico debug ====\n");
    printf("board        : %s\n", ICE_BOARD_NAME);
    printf("clock        : %u kHz\n", (unsigned)SYS_CLOCK_KHZ);
    printf("BIOS D0..D7  : GP%u..GP%u\n", BIOS_D0_PIN, BIOS_D0_PIN + 7);
    printf("BIOS CE (W)  : GP%u   read only where the asm tests CS (resume, xcdvdman/logo\n"
           "                      first byte, mech region exit, SCEx, sleep wake)\n", BIOS_CE_PIN);
    printf("BIOS bytes   : every /OE strobe as in the SX28, %s\n",
           BIOS_QUALIFY == BIOS_QUALIFY_OE_WIDTH ? "strobes over before ~80 ns dropped"
                                                 : "no width filter");
    printf("BIOS OE (R)  : GP%u   trigger; sample +%u cycles (%u ns), strobe check +%u cycles (%u ns) after that\n",
           BIOS_OE_PIN, BIOS_SAMPLE_DELAY_CYCLES,
           (unsigned)(BIOS_SAMPLE_DELAY_CYCLES * ICE_CYCLE_PS / 1000u),
           BIOS_CHECK_CYCLES, (unsigned)(BIOS_CHECK_CYCLES * ICE_CYCLE_PS / 1000u));
    printf("RESET / EJECT / SCEX : GP%u / GP%u / GP%u\n", RESET_PIN, EJECT_PIN, SCEX_PIN);
    printf("mech B,G,H,I : GP%u..GP%u (PIO)   F jumper : GP%u (GPIO, pull-up)\n",
           MECH_B_PIN, MECH_B_PIN + 3, MECH_F_PIN);
    printf("mech OE      : %s on GP%u, sample +%u cycles (%u ns)\n",
           mech_mode_name(), MECH_EDGE_PIN, MECH_SAMPLE_DELAY_CYCLES,
           (unsigned)(MECH_SAMPLE_DELAY_CYCLES * ICE_CYCLE_PS / 1000u));
#if !defined(MECH_OE_FLIPFLOP)
    printf("mech write   : +%u cycles (%u ns) after A falls\n", MECH_WRITE_DELAY_CYCLES,
           (unsigned)(MECH_WRITE_DELAY_CYCLES * ICE_CYCLE_PS / 1000u));
#endif
    printf("mech patch   : %s\n", MECH_PATCH ? "ON" : "OFF (test build: mechacon bus never driven)");
#if defined(MECH_OE_FLIPFLOP)
    printf("mech /1R     : GP%u\n", MECH_FF_R_PIN);
#endif
    printf("features     : RSTBUMP=%d H2O75KJMPERS=%d NTSCPS1YFIX75K=%d region=%s dev1=%s\n",
           RSTBUMP, H2O75KJMPERS, NTSCPS1YFIX75K,
#if defined(JAPv14orv8)
           "JAPv14orv8",
#elif defined(USAv14)
           "USAv14",
#else
           "BIOS",
#endif
#if defined(ICEFINALDEV1)
           "ICE FINAL"
#else
           "H2O"
#endif
    );
    printf("commands     : c=config  p=pin levels  m=mech trace  e=CE/OE/A timing  r=reboot\n");
    printf("============================\n");
}

static void print_pins(void)
{
    uint32_t g = gpio_get_all();
    printf("pins: D=%02x CE=%u OE=%u RST=%u EJ=%u S=%u  mech FBGHI=%u%u%u%u%u  edge=%u\n",
           (unsigned)((g >> BIOS_D0_PIN) & 0xff),
           (unsigned)((g >> BIOS_CE_PIN) & 1), (unsigned)((g >> BIOS_OE_PIN) & 1),
           (unsigned)((g >> RESET_PIN) & 1), (unsigned)((g >> EJECT_PIN) & 1),
           (unsigned)((g >> SCEX_PIN) & 1),
           (unsigned)((g >> MECH_F_PIN) & 1), (unsigned)((g >> MECH_B_PIN) & 1),
           (unsigned)((g >> MECH_G_PIN) & 1), (unsigned)((g >> MECH_H_PIN) & 1),
           (unsigned)((g >> MECH_I_PIN) & 1), (unsigned)((g >> MECH_EDGE_PIN) & 1));
}

static void print_mech_trace(void)
{
    uint32_t n = mech_trace_n;
    printf("mech: %lu words read so far, last %u (oldest first), shown as IHGB nibble:\n",
           (unsigned long)n, MECH_TRACE_LEN);
    for (int k = MECH_TRACE_LEN; k > 0; k--) {
        if (n < (uint32_t)k) continue;
        uint8_t w = mech_trace[(n - k) % MECH_TRACE_LEN];
        printf("%x ", (unsigned)(w & 0xf));          /* bit3=I bit2=H bit1=G bit0=B */
        if (k % 16 == 1) putchar('\n');
    }
    printf("(I,B: FA=I1B0 -> '8'/'a'/'c'/'e', FF=I1B1 -> '9'/'b'/'d'/'f', 00/01=I0B0 -> '0'/'2'/'4'/'6')\n");
}

/* ---- 'e': bus timing as seen through the Pico's input pins -------------
 * Samples SIO gpio_in into RAM in an unrolled loop (~3-4 cycles = 15-20 ns
 * per sample at 200 MHz), then measures.  Run it while the BIOS is busy
 * (console booting) - an idle bus shows nothing. */
#define EBUF_N 49152
static uint32_t s_ebuf[EBUF_N / 2];          /* 2 x 16-bit samples per word */

static void __not_in_flash_func(bus_capture)(uint16_t *buf, uint32_t n, uint shift)
{
    const volatile uint32_t *in = &sio_hw->gpio_in;
    for (uint32_t i = 0; i < n; i += 8) {
        buf[i + 0] = (uint16_t)(*in >> shift); buf[i + 1] = (uint16_t)(*in >> shift);
        buf[i + 2] = (uint16_t)(*in >> shift); buf[i + 3] = (uint16_t)(*in >> shift);
        buf[i + 4] = (uint16_t)(*in >> shift); buf[i + 5] = (uint16_t)(*in >> shift);
        buf[i + 6] = (uint16_t)(*in >> shift); buf[i + 7] = (uint16_t)(*in >> shift);
    }
}

static void print_bus_stats(void)
{
    /* CE, OE and A must fit in one 16-bit window */
    const uint lo = MIN(MIN(BIOS_CE_PIN, BIOS_OE_PIN), MECH_EDGE_PIN);
    const uint hi = MAX(MAX(BIOS_CE_PIN, BIOS_OE_PIN), MECH_EDGE_PIN);
    if (hi - lo > 15) { printf("e: CE/OE/A pins too far apart for the sampler\n"); return; }
    const uint ce = BIOS_CE_PIN - lo, oe = BIOS_OE_PIN - lo, a = MECH_EDGE_PIN - lo;
    uint16_t *b = (uint16_t *)s_ebuf;

    /* wait (max 2 s) for BIOS activity so the window is not empty */
    absolute_time_t until = make_timeout_time_ms(2000);
    while (gpio_get(BIOS_CE_PIN) && !time_reached(until)) { }

    uint32_t t0 = time_us_32();
    uint32_t irq = save_and_disable_interrupts();
    bus_capture(b, EBUF_N, lo);
    restore_interrupts(irq);
    uint32_t us = time_us_32() - t0;
    float ns = us ? (us * 1000.0f) / EBUF_N : 0;

    uint32_t ce_pulses = 0, ce_short = 0, ce_w_min = ~0u, ce_w_max = 0, ce_w_sum = 0;
    uint32_t gap_min = ~0u, gaps_short = 0;
    uint32_t oe_falls = 0, oe_ce_low = 0, oe_ce_high = 0, oe_ce_late = 0;
    uint32_t a_pulses = 0, a_short = 0, a_w_min = ~0u, a_w_max = 0;
    uint32_t ce_fall_at = 0, ce_rise_at = 0, a_fall_at = 0;
    bool have_ce_rise = false;
    const uint32_t chk = (uint32_t)(55.0f / (ns > 0 ? ns : 1) + 0.999f);  /* PIO check point */

    for (uint32_t i = 1; i < EBUF_N; i++) {
        uint16_t p = b[i - 1], c = b[i];
        bool pce = (p >> ce) & 1, cce = (c >> ce) & 1;
        bool poe = (p >> oe) & 1, coe = (c >> oe) & 1;
        bool pa  = (p >> a) & 1,  ca  = (c >> a) & 1;
        if (pce && !cce) {
            ce_fall_at = i;
            if (have_ce_rise) {
                uint32_t g = i - ce_rise_at;
                if (g < gap_min) gap_min = g;
                if (g * ns < 100) gaps_short++;
            }
        }
        if (!pce && cce && ce_fall_at) {
            uint32_t w = i - ce_fall_at;
            ce_pulses++; ce_w_sum += w;
            if (w < ce_w_min) ce_w_min = w;
            if (w > ce_w_max) ce_w_max = w;
            if (w * ns < 150) ce_short++;
            ce_rise_at = i; have_ce_rise = true;
        }
        if (poe && !coe && i + chk < EBUF_N) {
            oe_falls++;
            bool ce_at_chk = (b[i + chk] >> ce) & 1;
            if (!ce_at_chk) oe_ce_low++;
            else {
                oe_ce_high++;
                /* CE high at the check point but low a little later? = CE late */
                for (uint32_t k = chk; k < chk + (uint32_t)(150 / (ns > 0 ? ns : 1)) && i + k < EBUF_N; k++)
                    if (!((b[i + k] >> ce) & 1)) { oe_ce_late++; break; }
            }
        }
        if (pa && !ca) a_fall_at = i;
        if (!pa && ca && a_fall_at) {
            uint32_t w = i - a_fall_at;
            a_pulses++;
            if (w < a_w_min) a_w_min = w;
            if (w > a_w_max) a_w_max = w;
            if (w * ns < 100) a_short++;
        }
    }

    printf("\n=== bus timing seen by the Pico: %u samples in %lu us (%.1f ns/sample) ===\n",
           EBUF_N, (unsigned long)us, (double)ns);
    if (!ce_pulses) {
        printf("BIOS CE: no low pulses in the window (ROM idle?) - run 'e' while the console boots\n");
    } else {
        printf("BIOS CE low pulses : %lu, width min %.0f / avg %.0f / max %.0f ns   [DSLogic: 210 or 860-870 ns]\n",
               (unsigned long)ce_pulses, (double)(ce_w_min * ns),
               (double)(ce_w_sum * ns / ce_pulses), (double)(ce_w_max * ns));
        printf("BIOS CE high gaps  : min %.0f ns, %lu under 100 ns          [DSLogic: 20-30 ns between words]\n",
               (double)(gap_min == ~0u ? 0 : gap_min * ns), (unsigned long)gaps_short);
        printf("BIOS CE < 150 ns   : %lu                                    [DSLogic: ~0 outside glitches]\n",
               (unsigned long)ce_short);
    }
    printf("BIOS OE strobes    : %lu; CE low at the PIO check point (+55 ns): %lu, CE high: %lu\n",
           (unsigned long)oe_falls, (unsigned long)oe_ce_low, (unsigned long)oe_ce_high);
    printf("  CE LATE (high at +55 ns, low within +150 ns): %lu  <- should be 0; >0 = CE input too slow\n",
           (unsigned long)oe_ce_late);
    printf("                                               [DSLogic: CE falls 10-20 ns BEFORE the first OE]\n");
    if (a_pulses)
        printf("mech A low pulses  : %lu, width min %.0f / max %.0f ns, %lu under 100 ns  [DSLogic: 260-320 ns]\n",
               (unsigned long)a_pulses, (double)(a_w_min * ns), (double)(a_w_max * ns),
               (unsigned long)a_short);
    else
        printf("mech A             : no strobes in this window\n");
    printf("(resolution ~%.0f ns; widths are +-1 sample)\n", (double)ns);
}

static void print_event(const dbg_ev_t *e)
{
    printf("[%7lu.%03lu ms] ", (unsigned long)(e->t_us / 1000u),
           (unsigned long)(e->t_us % 1000u));
    if (e->fmt == NULL) {
        printf("-> %s\n", ice_state_name((int)e->a));
        return;
    }
    printf(e->fmt, e->a, e->b, e->c, e->d, e->e, e->f);
    putchar('\n');
}

void dbg_run(void (*ice_main)(void))
{
    queue_init(&s_q, sizeof(dbg_ev_t), ICE_DEBUG_QUEUE_LEN);
    stdio_usb_init();

    multicore_launch_core1_with_stack(ice_main, s_core1_stack, sizeof s_core1_stack);

    bool     was_connected = false;
    uint32_t dropped_seen  = 0;

    for (;;) {
        bool connected = stdio_usb_connected();
        if (connected && !was_connected) {
            sleep_ms(50);                    /* let the terminal settle */
            print_banner();
            print_pins();
        }
        was_connected = connected;

        if (connected) {
            dbg_ev_t e;
            int n = 0;
            while (n++ < 32 && queue_try_remove(&s_q, &e)) print_event(&e);

            uint32_t d = s_dropped;
            if (d != dropped_seen) {
                printf("!! %lu debug events dropped (queue full)\n",
                       (unsigned long)(d - dropped_seen));
                dropped_seen = d;
            }

            int c = getchar_timeout_us(0);
            if (c == 'c' || c == 'C') print_banner();
            if (c == 'p' || c == 'P') print_pins();
            if (c == 'm' || c == 'M') print_mech_trace();
            if (c == 'e' || c == 'E') print_bus_stats();
            if (c == 'r' || c == 'R') {
                printf("rebooting...\n");
                sleep_ms(20);
                watchdog_reboot(0, 0, 0);
            }
        }

        console_standby_watch();
        sleep_us(500);
    }
}

#endif /* ICE_DEBUG */
