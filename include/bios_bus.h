/*
 * bios_bus.h - BIOS (boot ROM) bus: sniffing reads and overdriving bytes.
 *
 * Same model as the SX28 asm:
 *   - a BIOS byte is an /OE low strobe (BIOS_WAIT_OE_LO); CE is NOT used to
 *     qualify bytes, skips or patch cycles.  The PIO only drops strobes that
 *     are already over at the ~80 ns check point (8-56 ns non-ROM strobes,
 *     whose data is garbage) - see BIOS_QUALIFY in config.h.
 *   - CE (CS) is looked at only where the asm does "sb/snb IO_BIOS_CS":
 *       RESUME_MODE_FROM_EJECT ........ bios_ce_low()        (level)
 *       IS_XCDVDMAN / PS2LOGO first byte bios_sync_*(.., BIOS_CE_FIRST)
 *                                       (CE level recorded with that byte)
 *       ALL_CDDVD_PATCH1_GET_SYNC_BIT .. gpio level (media_patch.c)
 *       SCEX_LOW ....................... bios_ce_low()        (scex.c)
 *       sleep wake-up .................. GPIO edge IRQ        (console_io.c)
 *
 * The sniffer pushes one 32-bit word per strobe: every GPIO level at the
 * sample point, rotated so bits 0..7 are D0..D7.  BIOS_W_DATA / BIOS_W_CE_LOW
 * pick the parts out.
 */
#ifndef ICE_BIOS_BUS_H
#define ICE_BIOS_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hardware/pio.h"
#include "console_io.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/structs/io_bank0.h"
#include "config.h"

#define BIOS_PIO        pio0
#define BIOS_SNIFF_SM   0
#define BIOS_OUT_SM     1
#define BIOS_OUT_IRQ    0       /* writer -> CPU: bus released            */
#define BIOS_GO_IRQ     7       /* CPU -> writer: fire (PIO-internal flag) */

/* sniffer word layout */
#define BIOS_CE_BIT          ((BIOS_CE_PIN - BIOS_D0_PIN) & 31)
#define BIOS_W_DATA(w)       ((uint8_t)(w))
#define BIOS_W_CE_LOW(w)     ((((w) >> BIOS_CE_BIT) & 1u) == 0)

void bios_bus_init(void);

/* Drop anything already captured (use after delays / non-bus activity). */
void bios_flush(void);

/* Level of the BIOS chip select right now (SX28: sb/snb IO_BIOS_CS). */
static inline bool bios_ce_low(void) { return !gpio_get(BIOS_CE_PIN); }

/* Debug only (heartbeat): has CE gone low since the last clear?  Raw GPIO
 * edge latch.  Not used for any decision the asm makes. */
static inline bool bios_ce_fell(void)
{
    return (io_bank0_hw->intr[BIOS_CE_PIN / 8] >> (4 * (BIOS_CE_PIN % 8))) & GPIO_IRQ_EDGE_FALL;
}
static inline void bios_ce_fell_clear(void)
{
    gpio_acknowledge_irq(BIOS_CE_PIN, GPIO_IRQ_EDGE_FALL);
}

static inline bool bios_rx_empty(void)
{
    return (BIOS_PIO->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + BIOS_SNIFF_SM))) != 0;
}

/* Blocking: raw word of the next strobe.
 * The SX28-interrupt check (reset / tray, see console_io.h) costs two
 * IO_BANK0 reads, so it only runs every 256th idle pass (~1-2 us): the
 * plain spin stays ~4 cycles, so a byte is picked up within a few ns.  A
 * patch fired after a sync has only one ROM strobe (216 ns) to spare. */
#define BIOS_ISR_POLL_MASK 0xffu
static inline uint32_t bios_word(void)
{
    uint32_t spin = 0;
    while (bios_rx_empty())
        if ((++spin & BIOS_ISR_POLL_MASK) == 0) ice_isr_poll();
    return BIOS_PIO->rxf[BIOS_SNIFF_SM];
}

/* Blocking: data byte of the next strobe (BIOS_WAIT_OE_LO + mov w,rc). */
static inline uint8_t bios_byte(void) { return BIOS_W_DATA(bios_word()); }

/* Skip n strobes (the "sb OE / snb OE / decsz" loops). */
static inline void bios_skip(uint32_t n)
{
    while (n--) (void)bios_word();
}

/* ---- sync matching ------------------------------------------------------
 * Match 1..4 bytes on consecutive strobes.  The last bytes are kept in a
 * shift register, so a mismatching byte is still considered as the start
 * of the next attempt ("03 03 24" finds "03 24").
 * BIOS_SEQn(...) packs the bytes in bus order.
 */
#define BIOS_SEQ1(a)          ((uint32_t)(a))
#define BIOS_SEQ2(a,b)        (((uint32_t)(a) << 8)  | (uint32_t)(b))
#define BIOS_SEQ3(a,b,c)      (((uint32_t)(a) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(c))
#define BIOS_SEQ4(a,b,c,d)    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                               ((uint32_t)(c) << 8)  | (uint32_t)(d))

/* flag for bios_sync_until / bios_sync_idle: the first byte of the sequence
 * must have been read while CS was low (IS_XCDVDMAN loopx / PS2LOGO loopx:
 * "sb IO_BIOS_CS ; jmp ...loop1" then compare). */
#define BIOS_CE_FIRST  true
#define BIOS_ANY_CE    false

static inline uint32_t bios_seq_mask(unsigned n)
{
    return n >= 4 ? 0xffffffffu : ((1u << (8 * n)) - 1u);
}

/* Blocking: return once the n-byte sequence has been read.
 * Kept tight on purpose: after the last byte matches, the caller fires the
 * writer, which has to see the fire before the next /OE strobe. */
static inline void bios_sync(uint32_t seq, unsigned n)
{
    const uint32_t mask = bios_seq_mask(n);
    uint32_t win = 0;
    for (unsigned i = 1; i < n; i++)        /* prime the window with n-1 bytes */
        win = (win << 8) | BIOS_W_DATA(bios_word());
    for (;;) {
        win = (win << 8) | BIOS_W_DATA(bios_word());
        if ((win & mask) == seq) return;
    }
}

/* Writer fire, defined further down */
static inline void bios_out_fire(void);

/* bios_sync() and fire the armed writer in the same instant the last byte
 * matched - no return / call in between (the asm writes right there too:
 * "sb z ; jmp ... ; mov !IO_BIOS_DATA,w"). */
static inline void bios_sync_fire(uint32_t seq, unsigned n)
{
    const uint32_t mask = bios_seq_mask(n);
    uint32_t win = 0;
    for (unsigned i = 1; i < n; i++)
        win = (win << 8) | BIOS_W_DATA(bios_word());
    for (;;) {
        win = (win << 8) | BIOS_W_DATA(bios_word());
        if ((win & mask) == seq) { bios_out_fire(); return; }
    }
}

/* skip n strobes, fire on the last one */
static inline void bios_skip_fire(uint32_t n)
{
    while (n > 1) { (void)bios_word(); n--; }
    (void)bios_word();
    bios_out_fire();
}

/* Same, but gives up (returns false) at `deadline`. */
bool bios_sync_until(uint32_t seq, unsigned n, bool ce_first, absolute_time_t deadline);

/* PS2LOGO flavour: gives up once BIOS CE has been HIGH (ROM idle) for a
 * total of *idle_budget_us while waiting - the SX28's loopx counters only
 * ran while CS was high.  The remaining budget is written back. */
bool bios_sync_idle(uint32_t seq, unsigned n, bool ce_first, int64_t *idle_budget_us);

/* Wait until a byte equal to `b` is read. */
static inline void bios_wait_byte(uint8_t b) { bios_sync(BIOS_SEQ1(b), 1); }
static inline void bios_wait_byte_fire(uint8_t b) { bios_sync_fire(BIOS_SEQ1(b), 1); }

/* ---- overdrive -------------------------------------------------------- */

/* One piece of a patch stream: `n` consecutive strobes, driven with
 * bytes[0..n-1], or - if bytes == NULL - left to the ROM (skipped). */
typedef struct {
    const uint8_t *bytes;
    uint16_t       n;
} bios_seg_t;

#define BIOS_OUT_MAX_CYCLES 256

/* Prepare the writer.  The first cycle is the one right after the strobe on
 * which bios_out_fire() is called.  The writer starts running and parks just
 * before the first drive, so firing costs one register write.
 * The data must stay valid until bios_out_finish() returns. */
void bios_out_arm_segs(const bios_seg_t *segs, size_t nseg);

static inline void bios_out_arm(const uint8_t *bytes, size_t n)
{
    bios_seg_t s = { bytes, (uint16_t)n };
    bios_out_arm_segs(&s, 1);
}

/* Start driving: call this the instant the last sync byte has matched. */
static inline void bios_out_fire(void)
{
    BIOS_PIO->irq_force = 1u << BIOS_GO_IRQ;
}

/* Feed the rest of the stream, read back what the bus carried on each
 * driven cycle, return once the bus has been released.  `tag` names the
 * patch in the debug log.  Returns the number of driven cycles whose byte
 * read back differently (0 = clean). */
size_t bios_out_finish(const char *tag);

static inline size_t bios_out_now(const uint8_t *bytes, size_t n, const char *tag)
{
    bios_out_arm(bytes, n);
    bios_out_fire();
    return bios_out_finish(tag);
}

/* True (and cleared) if the sniffer FIFO overflowed since the last flush. */
bool bios_overflow_check(void);

#endif
