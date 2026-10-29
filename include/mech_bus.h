/*
 * mech_bus.h - mechacon CD/DVD data bus (B,G,H,I) and its OE edge "A".
 *
 * Bit layout of a sniffed word / of the SX28 port B it replaces:
 *      here   SX28 rb    pad
 *      bit0   rb.4       B   (CDDVD D2)
 *      bit1   rb.5       G
 *      bit2   rb.6       H
 *      bit3   rb.7       I   (CDDVD D7)
 * F (rb.0) is never part of a mechacon test in the asm ($90/$b0/$a0 masks
 * skip it) and never driven onto the bus: here it is only the H2O75KJMPERS
 * jumper input - a plain GPIO with pull-up, outside the PIO.
 *
 * The asm masks translate as:  $90 -> I|B, $b0 -> I|G|B, $a0 -> I|G.
 * Nibbles written with `mov IO_CDDVD_BUS,w` land on rb4..rb7 = B,G,H,I,
 * i.e. nibble bit0 = B, bit1 = G, bit2 = H, bit3 = I - same order as
 * MECH_NIB_* below, so the CDDVD_PATCH_DATA bytes are used unchanged.
 *
 * MECH_OE_DIRECT  : A on MECH_A_PIN, PIO latches the edge (no flip-flop).
 * MECH_OE_FLIPFLOP: /1Q on MECH_FF_Q_PIN, /1R driven by PIO.
 * Same split as the BIOS bus: the OE/edge pin is a separate trigger input
 * (any GPIO, "wait gpio" retargeted at load time), B,G,H,I are the only
 * pins in the 4-bit data word.
 */
#ifndef ICE_MECH_BUS_H
#define ICE_MECH_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hardware/pio.h"
#include "config.h"
#include "console_io.h"

#define MECH_PIO        pio1
#define MECH_READ_SM    0
#define MECH_OUT_SM     1
#define MECH_RISE_SM    2            /* debug read-back at A rising */

/* bus read-back at A rising: DIRECT wiring + debug build only */
#if ICE_DEBUG && !defined(MECH_OE_FLIPFLOP)
#define MECH_RISE_TRACE 1
#else
#define MECH_RISE_TRACE 0
#endif

#define MECH_B   (1u << 0)
#define MECH_G   (1u << 1)
#define MECH_H   (1u << 2)
#define MECH_I   (1u << 3)

/* nibble bits for the writer (B,G,H,I) */
#define MECH_NIB_B  (1u << 0)
#define MECH_NIB_G  (1u << 1)
#define MECH_NIB_H  (1u << 2)
#define MECH_NIB_I  (1u << 3)

void mech_bus_init(void);
void mech_flush(void);

static inline bool mech_rx_empty(void)
{
    return (MECH_PIO->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + MECH_READ_SM))) != 0;
}

/* Debug trace of the last words the CPU read (core 0 can dump it). */
#define MECH_TRACE_LEN 64
extern volatile uint8_t  mech_trace[MECH_TRACE_LEN];
extern volatile uint32_t mech_trace_n;

/* Debug heartbeat for long mechacon waits: once a second logs strobe rate,
 * whether BIOS CE fell, and the last words seen.  No-op without ICE_DEBUG. */
#if ICE_DEBUG
/* (re)start, name the wait; show_ce = this wait has a CS exit in the asm */
void mech_heartbeat_where(const char *where, bool show_ce);
void mech_heartbeat(void);
#else
static inline void mech_heartbeat_where(const char *where, bool show_ce) { (void)where; (void)show_ce; }
static inline void mech_heartbeat(void) { }
#endif

#if MECH_RISE_TRACE
/* mech_rise[i % MECH_TRACE_LEN] = bus at A rising of strobe i, the same
 * strobe numbering as mech_trace (mech_rise_sync() lines them up) */
extern volatile uint8_t  mech_rise[MECH_TRACE_LEN];
extern volatile uint32_t mech_rise_n;
static inline void mech_rise_poll(void)
{
    while (!(MECH_PIO->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + MECH_RISE_SM)))) {
        uint32_t n = mech_rise_n;
        mech_rise[n % MECH_TRACE_LEN] = (uint8_t)(MECH_PIO->rxf[MECH_RISE_SM] & 0x0f);
        mech_rise_n = n + 1;
    }
}
#else
static inline void mech_rise_poll(void) { }
#endif
/* flush the reader and restart the read-back in step with it (A idle) */
void mech_rise_sync(void);

/* Store one read word in the trace and return it. */
static inline uint8_t mech_take(void)
{
    mech_rise_poll();
    uint8_t w = (uint8_t)(MECH_PIO->rxf[MECH_READ_SM] & 0x0f);
    uint32_t n = mech_trace_n;
    mech_trace[n % MECH_TRACE_LEN] = w;
    mech_trace_n = n + 1;
    return w;
}

/* Blocking: bus value at the next OE edge (= "wait /1Q low, read rb,
 * reset flip-flop" in the asm). */
static inline uint8_t mech_read(void)
{
    while (mech_rx_empty()) { ice_isr_poll(); mech_heartbeat(); }
    return mech_take();
}

/* MECHACON_WAIT_OE: let n edges pass. */
static inline void mech_wait_oe(uint32_t n)
{
    while (n--) (void)mech_read();
}

/* Level read of the pads outside of an edge (the H2O75KJMPERS jumper tests
 * "sb IO_CDDVD_BUS_f" / "sb IO_CDDVD_BUS_h" read the port directly). */
bool mech_pin_f(void);
bool mech_pin_h(void);

/* ---- writer ------------------------------------------------------------ */

/* Queue one writer action.  dirs/data use MECH_NIB_* bits.
 *   immediate = true : apply now  (the asm wrote !rb right after a sync edge)
 *   immediate = false: apply at the next OE edge                           */
void mech_out(uint8_t data, uint8_t dirs, bool immediate);

/* Queue "release the bus" (mov !IO_CDDVD_BUS,#$ff), immediately or at edge. */
static inline void mech_release(bool immediate) { mech_out(0, 0, immediate); }

/* Block until every queued action has been applied. */
void mech_out_wait(void);

/* Pre-armed writer (like the BIOS writer): load the words into the STOPPED
 * writer before the sync, then mech_out_fire() the instant the last sync
 * strobe matched - one register write, so the first word lands on the very
 * next A edge as in the asm (strobes are only ~770 ns apart; building and
 * queueing the words after the match took ~1 us and shifted the patch).
 * Writer word (pio/mech_bus.pio): bit0 immediate, bits 5..1 pre-delay
 * (immediate words, x4 PIO cycles), bits 9..6 data B,G,H,I, 13..10 dirs. */
#define MECH_WORD_DLY(data, dirs, now, dly4) \
    (((now) ? 1u : 0u) | ((uint32_t)((dly4) & 0x1f) << 1) | \
     ((uint32_t)((data) & 0xf) << 6) | ((uint32_t)((dirs) & 0xf) << 10))
#define MECH_WORD(data, dirs, now)  MECH_WORD_DLY(data, dirs, now, 0)
void mech_out_arm(const uint32_t *w, size_t n);
static inline void mech_out_fire(void)
{
    hw_set_bits(&MECH_PIO->ctrl, 1u << (PIO_CTRL_SM_ENABLE_LSB + MECH_OUT_SM));
}
/* feed the rest of the armed words, let `edges` reader words go by (traced),
 * return when every word has been applied */
void mech_out_finish(size_t edges);

#endif
