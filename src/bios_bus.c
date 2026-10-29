#include "bios_bus.h"
#include "bios_bus.pio.h"
#include "pio_patch.h"
#include "debug_log.h"
#include "pico/time.h"

static uint          s_out_offset;
static uint16_t      s_words[BIOS_OUT_MAX_CYCLES];  /* data | dirs<<8 per cycle */
static size_t        s_out_total;   /* cycles in the stream = words to read back */
static size_t        s_out_fed;     /* words already in the TX FIFO */

static pio_patched_t s_sniff_prog, s_out_prog;

/* time allowed for a whole patch stream before the writer is aborted */
#define BIOS_OUT_TIMEOUT_US  2000000u

static void bus_pad_setup(uint pin, bool output_capable)
{
    gpio_disable_pulls(pin);                 /* RP2040 pads default to pull-down */
    gpio_set_input_enabled(pin, true);
    if (output_capable) {
        /* overdriving the ROM: strongest drive, fast edges */
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_slew_rate(pin, GPIO_SLEW_RATE_FAST);
    }
}

void bios_bus_init(void)
{
    PIO pio = BIOS_PIO;

    /* CE and OE: plain GPIO inputs, never driven.  OE is the PIO trigger
     * ("wait gpio") and width check ("jmp pin"); CE is only recorded in the
     * sniffer word and read by the CPU where the asm tests IO_BIOS_CS. */
    gpio_init(BIOS_CE_PIN);
    gpio_set_dir(BIOS_CE_PIN, GPIO_IN);
    bus_pad_setup(BIOS_CE_PIN, false);
    gpio_init(BIOS_OE_PIN);
    gpio_set_dir(BIOS_OE_PIN, GPIO_IN);
    bus_pad_setup(BIOS_OE_PIN, false);
    for (uint i = 0; i < 8; i++) bus_pad_setup(BIOS_D0_PIN + i, true);

#if BIOS_QUALIFY == BIOS_QUALIFY_NONE
    const int gate_sniff = (int)bios_sniff_offset_gate;   /* -> NOP */
    const int gate_out   = (int)bios_out_offset_gate;
#else
    const int gate_sniff = PIO_PATCH_NONE;                /* OE width check */
    const int gate_out   = PIO_PATCH_NONE;
#endif

    /* sample point and "still low?" check point, both from ns (config.h).
     * sniffer: wait[d] -> in -> nop[c-2] -> jmp pin   (jmp at d+c+1 cycles)
     * writer : wait[d] ->       nop[c-1] -> jmp pin   (same point)        */
    const pio_program_t *sp = pio_patch(&s_sniff_prog, &bios_sniff_program, BIOS_OE_PIN,
                                        bios_sniff_offset_sample, BIOS_SAMPLE_DELAY_CYCLES,
                                        gate_sniff);
    pio_patch_delay(&s_sniff_prog, bios_sniff_offset_chk, BIOS_CHECK_CYCLES - 2);
    const pio_program_t *op = pio_patch(&s_out_prog, &bios_out_program, BIOS_OE_PIN,
                                        bios_out_offset_sample, BIOS_SAMPLE_DELAY_CYCLES,
                                        gate_out);
    pio_patch_delay(&s_out_prog, bios_out_offset_chk, BIOS_CHECK_CYCLES - 1);
    uint sniff_off = pio_add_program(pio, sp);
    s_out_offset   = pio_add_program(pio, op);

    bios_sniff_program_init(pio, BIOS_SNIFF_SM, sniff_off, BIOS_D0_PIN, BIOS_OE_PIN);
    bios_out_program_init(pio, BIOS_OUT_SM, s_out_offset, BIOS_D0_PIN, BIOS_OE_PIN);

    pio->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + BIOS_SNIFF_SM);   /* clear */
    pio_sm_set_enabled(pio, BIOS_SNIFF_SM, true);
    /* writer stays disabled until armed */

    DBG("bios bus up: D0-7=GP%u-%u CE=GP%u OE=GP%u, sample +%u cycles, check +%u cycles",
        BIOS_D0_PIN, BIOS_D0_PIN + 7, BIOS_CE_PIN, BIOS_OE_PIN, BIOS_SAMPLE_DELAY_CYCLES,
        BIOS_CHECK_CYCLES);
    DBG("bios bus up: clock %u kHz", SYS_CLOCK_KHZ);
    DBG("bios bus up: byte = every /OE strobe (as SX28), %s; CE only at the asm's CS checks",
        BIOS_QUALIFY == BIOS_QUALIFY_NONE ? "no width filter"
                                          : "strobes < ~80 ns dropped");
}

void bios_flush(void)
{
    while (!bios_rx_empty()) (void)BIOS_PIO->rxf[BIOS_SNIFF_SM];
    /* every sync starts with a flush: forget overflows from the time nobody
     * was reading (expected and harmless) */
    BIOS_PIO->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + BIOS_SNIFF_SM);
}

bool bios_overflow_check(void)
{
    const uint32_t bit = 1u << (PIO_FDEBUG_RXSTALL_LSB + BIOS_SNIFF_SM);
    if (BIOS_PIO->fdebug & bit) {
        BIOS_PIO->fdebug = bit;              /* write 1 to clear */
        return true;
    }
    return false;
}

/* window match + "first byte had CS low" (bit n-1 of the CE-low history) */
#define CE_FIRST_OK(cew, n, ce_first)  (!(ce_first) || (((cew) >> ((n) - 1)) & 1u))

bool __not_in_flash_func(bios_sync_until)(uint32_t seq, unsigned n, bool ce_first,
                                          absolute_time_t deadline)
{
    const uint32_t mask = bios_seq_mask(n);
    uint32_t win = 0, cew = 0;
    unsigned have = 0;
    for (;;) {
        uint32_t spin = 0;
        while (bios_rx_empty()) {
            if ((++spin & BIOS_ISR_POLL_MASK) == 0) {
                ice_isr_poll();
                if (time_reached(deadline)) return false;
            }
        }
        uint32_t w = BIOS_PIO->rxf[BIOS_SNIFF_SM];
        win = (win << 8) | BIOS_W_DATA(w);
        cew = (cew << 1) | (BIOS_W_CE_LOW(w) ? 1u : 0u);
        if (have < n) { if (++have < n) continue; }
        if ((win & mask) == seq && CE_FIRST_OK(cew, n, ce_first)) return true;
    }
}

bool __not_in_flash_func(bios_sync_idle)(uint32_t seq, unsigned n, bool ce_first,
                                         int64_t *idle_budget_us)
{
    const uint32_t mask = bios_seq_mask(n);
    uint32_t win = 0, cew = 0;
    unsigned have = 0;
    int64_t  budget = *idle_budget_us;
    uint32_t last = time_us_32();

    for (;;) {
        uint32_t spin = 0;
        while (bios_rx_empty()) {
            if ((++spin & BIOS_ISR_POLL_MASK) == 0) ice_isr_poll();
            uint32_t now = time_us_32();
            if (!bios_ce_low()) {            /* ROM idle: the SX28 counters tick */
                budget -= (int64_t)(now - last);
                if (budget <= 0) { *idle_budget_us = 0; return false; }
            }
            last = now;
        }
        uint32_t w = BIOS_PIO->rxf[BIOS_SNIFF_SM];
        last = time_us_32();
        win = (win << 8) | BIOS_W_DATA(w);
        cew = (cew << 1) | (BIOS_W_CE_LOW(w) ? 1u : 0u);
        if (have < n) { if (++have < n) continue; }
        if ((win & mask) == seq && CE_FIRST_OK(cew, n, ce_first)) {
            *idle_budget_us = budget;
            return true;
        }
    }
}

static inline bool tx_full(void)
{
    return (BIOS_PIO->fstat & (1u << (PIO_FSTAT_TXFULL_LSB + BIOS_OUT_SM))) != 0;
}

static inline void feed(void)
{
    while (s_out_fed < s_out_total && !tx_full())
        BIOS_PIO->txf[BIOS_OUT_SM] = s_words[s_out_fed++];
}

void bios_out_arm_segs(const bios_seg_t *segs, size_t nseg)
{
    PIO pio = BIOS_PIO;

    /* build the per-cycle stream */
    size_t k = 0;
    for (size_t s = 0; s < nseg; s++) {
        for (uint16_t i = 0; i < segs[s].n && k < BIOS_OUT_MAX_CYCLES; i++, k++) {
            s_words[k] = segs[s].bytes ? (uint16_t)(0xff00u | segs[s].bytes[i])
                                       : (uint16_t)0x0000u;      /* not driven */
        }
    }
    s_out_total = k;
    s_out_fed   = 0;

    pio_sm_set_enabled(pio, BIOS_OUT_SM, false);
    pio_sm_clear_fifos(pio, BIOS_OUT_SM);
    pio_sm_restart(pio, BIOS_OUT_SM);
    pio_sm_exec(pio, BIOS_OUT_SM, pio_encode_jmp(s_out_offset));
    pio_interrupt_clear(pio, BIOS_OUT_IRQ);
    pio_interrupt_clear(pio, BIOS_GO_IRQ);

    pio->txf[BIOS_OUT_SM] = (uint32_t)(k - 1);           /* count-1 */
    feed();

    /* run up to "wait 1 irq 7" (pulls count and the first word) */
    pio_sm_set_enabled(pio, BIOS_OUT_SM, true);
    const uint armed_pc = s_out_offset + bios_out_offset_armed;
    uint32_t t0 = time_us_32();
    while (pio_sm_get_pc(pio, BIOS_OUT_SM) != armed_pc) {
        if (time_us_32() - t0 > 100) break;              /* cannot happen */
    }
    feed();                                              /* top up */
}

static void bios_out_abort(void)
{
    PIO pio = BIOS_PIO;
    pio_sm_set_enabled(pio, BIOS_OUT_SM, false);
    pio_sm_set_pindirs_with_mask(pio, BIOS_OUT_SM, 0, 0xffu << BIOS_D0_PIN);
    pio_interrupt_clear(pio, BIOS_OUT_IRQ);
    pio_interrupt_clear(pio, BIOS_GO_IRQ);
}

static uint8_t s_echo[BIOS_OUT_MAX_CYCLES];

size_t __not_in_flash_func(bios_out_finish)(const char *tag)
{
    PIO pio = BIOS_PIO;
    const size_t total = s_out_total;
    size_t echoed = 0;
    uint32_t spins = 0, t0 = time_us_32();
    io_ro_32 *fstat = &pio->fstat;
    const uint32_t rx_empty = 1u << (PIO_FSTAT_RXEMPTY_LSB + BIOS_SNIFF_SM);
    const uint32_t tx_full_b = 1u << (PIO_FSTAT_TXFULL_LSB + BIOS_OUT_SM);

    /* Hot loop: a ROM strobe every ~216 ns = 43 cycles @ 200 MHz / 52 @
     * 240 MHz.  Only move words here (two FIFO checks per pass) and compare
     * afterwards, so long streams never fall behind the sniffer. */
    while (echoed < total) {
        uint32_t f = *fstat;
        if (!(f & rx_empty)) s_echo[echoed++] = (uint8_t)pio->rxf[BIOS_SNIFF_SM];
        if (s_out_fed < total && !(f & tx_full_b)) pio->txf[BIOS_OUT_SM] = s_words[s_out_fed++];
        if (((++spins) & 0xff) == 0 && time_us_32() - t0 > BIOS_OUT_TIMEOUT_US) {
            bios_out_abort();
            DBG("patch %s: TIMEOUT after %u/%u cycles - bus released", tag,
                (unsigned)echoed, (unsigned)total);
            return total;
        }
    }
    /* bus released? (irq 0 is raised after "out pindirs" of zeros) */
    while (!pio_interrupt_get(pio, BIOS_OUT_IRQ)) {
        if (time_us_32() - t0 > BIOS_OUT_TIMEOUT_US) { bios_out_abort(); break; }
    }
    pio_interrupt_clear(pio, BIOS_OUT_IRQ);
    pio_sm_set_enabled(pio, BIOS_OUT_SM, false);
    bool overflow = bios_overflow_check();

    /* read-back check on driven cycles */
    size_t driven = 0, bad = 0, first_bad = 0;
    uint8_t first_drove = 0, first_read = 0;
    for (size_t i = 0; i < total; i++) {
        if (!(s_words[i] & 0xff00u)) continue;
        driven++;
        if (s_echo[i] != (uint8_t)s_words[i]) {
            if (bad == 0) { first_bad = i; first_drove = (uint8_t)s_words[i]; first_read = s_echo[i]; }
            bad++;
        }
    }

    if (overflow) {
        DBG("!! patch %s: sniffer FIFO overflowed - strobes missed, read-back not valid", tag);
    } else if (bad == 0) {
        DBG("patch %s: %u bytes driven over %u cycles, read-back OK", tag,
            (unsigned)driven, (unsigned)total);
    } else {
        DBG("patch %s: %u/%u driven bytes read back WRONG", tag, (unsigned)bad,
            (unsigned)driven);
        DBG("   first at cycle #%u: drove %02x, bus had %02x (ROM won the bus?)",
            (unsigned)first_bad, first_drove, first_read);
    }
    return overflow ? total : bad;
}
