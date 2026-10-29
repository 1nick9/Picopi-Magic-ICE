/*
 * config.h - compile-time configuration for the Pico port of
 *            "H2O-Improved" / Magic ICE Final (SX28 build).
 *
 * Pin numbers live in include/boards/ice_board_<board>.h, chosen with
 * -DICE_BOARD=<board> in CMake (pico | waveshare_rp2040_tiny | yd_rp2040),
 * or by defining one ICE_BOARD_* macro below by hand.
 *
 * Original SX28 build being ported (see CMakeLists.txt):
 *      SX28, RSTBUMP, H2O75KJMPERS, H2O dev1 (ICEFINALDEV1 undefined),
 *      NTSCPS1YFIX75K
 */
#ifndef ICE_CONFIG_H
#define ICE_CONFIG_H

/* --------------------------------------------------------------------------
 * Board selection
 * ------------------------------------------------------------------------ */
#if defined(ICE_BOARD_WAVESHARE_RP2040_TINY)
#include "boards/ice_board_waveshare_rp2040_tiny.h"
#elif defined(ICE_BOARD_YD_RP2040)
#include "boards/ice_board_yd_rp2040.h"
#else   /* ICE_BOARD_PICO, or nothing selected */
#include "boards/ice_board_pico.h"
#endif

/* --------------------------------------------------------------------------
 * Feature switches (normally set by CMake; defaults below match the request)
 * ------------------------------------------------------------------------ */
#if !defined(MECH_OE_DIRECT) && !defined(MECH_OE_FLIPFLOP)
#define MECH_OE_DIRECT 1          /* default: no flip-flop, A straight to GPIO */
#endif
#if defined(MECH_OE_DIRECT) && defined(MECH_OE_FLIPFLOP)
#error "Choose only one of MECH_OE_DIRECT / MECH_OE_FLIPFLOP"
#endif

#ifndef RSTBUMP
#define RSTBUMP 0                 /* default F=TR: pad F drives the reset FET */
#endif
#ifndef H2O75KJMPERS
#define H2O75KJMPERS 0
#endif
#if defined(JAPv14orv8) && defined(USAv14)
#error "Define only one of JAPv14orv8 / USAv14"
#endif
#ifndef NTSCPS1YFIX75K
#define NTSCPS1YFIX75K 1
#endif
/* ICEFINALDEV1 undefined => H2O dev1 patch (default) */

/* USB debug log (stdio_usb, printed from core 1).  CMake: -DICE_DEBUG=ON */
#ifndef ICE_DEBUG
#define ICE_DEBUG 0
#endif

/* --------------------------------------------------------------------------
 * BIOS bus: which /OE strobes count as "a BIOS byte".
 * As in the SX28, CE is NOT part of this - every /OE strobe counts.  CE is
 * only read where the asm tests IO_BIOS_CS (see bios_bus.h).
 *
 * BIOS_QUALIFY_OE_WIDTH (default) drop strobes already over ~80 ns after the
 *                       fall: the 8-56 ns non-ROM strobes seen in the
 *                       captures (their "data" is garbage; the SX28's ~80 ns
 *                       polling loop missed most of them too).  ROM reads
 *                       are 128-136 ns.
 * BIOS_QUALIFY_NONE     every /OE strobe, however short.
 * ------------------------------------------------------------------------ */
#define BIOS_QUALIFY_NONE      0
#define BIOS_QUALIFY_OE_WIDTH  2
#ifndef BIOS_QUALIFY
#define BIOS_QUALIFY BIOS_QUALIFY_OE_WIDTH
#endif
#if BIOS_QUALIFY != BIOS_QUALIFY_NONE && BIOS_QUALIFY != BIOS_QUALIFY_OE_WIDTH
#error "BIOS_QUALIFY must be BIOS_QUALIFY_OE_WIDTH or BIOS_QUALIFY_NONE"
#endif

/* --------------------------------------------------------------------------
 * Derived pins
 * ------------------------------------------------------------------------ */
/* B,G,H,I: 4 consecutive GPIOs used by the PIO.  F is separate (jumper). */
#define MECH_G_PIN      (MECH_B_PIN + 1)
#define MECH_H_PIN      (MECH_B_PIN + 2)
#define MECH_I_PIN      (MECH_B_PIN + 3)

#if defined(MECH_OE_FLIPFLOP)
#define MECH_EDGE_PIN   MECH_FF_Q_PIN
#else
#define MECH_EDGE_PIN   MECH_A_PIN
#endif

/* --------------------------------------------------------------------------
 * Pin sanity checks
 * ------------------------------------------------------------------------ */
#define ICE_PIN_IN(p, lo, n)  ((p) >= (lo) && (p) < (lo) + (n))

#if BIOS_D0_PIN < 0 || BIOS_D0_PIN + 7 > 29
#error "BIOS D0..D7 must be 8 consecutive GPIOs inside GP0..GP29"
#endif
#if MECH_B_PIN < 0 || MECH_B_PIN + 3 > 29
#error "Mech B,G,H,I must be 4 consecutive GPIOs inside GP0..GP29"
#endif
#if ICE_PIN_IN(MECH_F_PIN, MECH_B_PIN, 4) || ICE_PIN_IN(MECH_F_PIN, BIOS_D0_PIN, 8) \
    || MECH_F_PIN == BIOS_CE_PIN || MECH_F_PIN == BIOS_OE_PIN
#error "Mech F (jumper) collides with another bus pin"
#endif
#if ICE_PIN_IN(BIOS_CE_PIN, BIOS_D0_PIN, 8) || ICE_PIN_IN(BIOS_OE_PIN, BIOS_D0_PIN, 8)
#error "BIOS CE/OE must not overlap BIOS D0..D7"
#endif
#if ICE_PIN_IN(BIOS_CE_PIN, MECH_B_PIN, 4) || ICE_PIN_IN(BIOS_OE_PIN, MECH_B_PIN, 4)
#error "BIOS CE/OE must not overlap mech B..I"
#endif
#if ICE_PIN_IN(MECH_B_PIN, BIOS_D0_PIN, 8) || ICE_PIN_IN(MECH_B_PIN + 3, BIOS_D0_PIN, 8)
#error "mech B..I overlaps BIOS D0..D7"
#endif
#if BIOS_CE_PIN == BIOS_OE_PIN
#error "BIOS CE and OE need their own GPIOs"
#endif
#if ICE_PIN_IN(MECH_EDGE_PIN, MECH_B_PIN, 4) || MECH_EDGE_PIN == MECH_F_PIN \
    || ICE_PIN_IN(MECH_EDGE_PIN, BIOS_D0_PIN, 8) \
    || MECH_EDGE_PIN == BIOS_CE_PIN || MECH_EDGE_PIN == BIOS_OE_PIN
#error "mech OE / flip-flop /1Q pin collides with another bus pin"
#endif

/* --------------------------------------------------------------------------
 * Timing
 * ------------------------------------------------------------------------ */
/* System clock.  240 MHz like fruitchip (VCO 1440 MHz / 6 / 1).  Above
 * 200 MHz the core voltage is raised to SYS_VREG_VOLTAGE first.
 * Every PIO delay below is given in NANOSECONDS and converted for this
 * clock, so changing the clock keeps the bus timing the same. */
#ifndef SYS_CLOCK_KHZ
#define SYS_CLOCK_KHZ            240000
#endif
#define SYS_VREG_VOLTAGE         VREG_VOLTAGE_1_15   /* used when > 200 MHz */

#define ICE_NS_TO_CYCLES(ns)     ((((ns) * (SYS_CLOCK_KHZ / 1000)) + 500) / 1000)
#define ICE_CYCLE_PS             (1000000000u / SYS_CLOCK_KHZ)   /* ps per cycle */

/* BIOS: time between the PIO seeing /OE low and sampling D0-D7 (+ the other
 * pins).  Logic sniffs: data valid ~40-80 ns after the edge.  40 ns on top of
 * the 2-cycle input synchroniser (= 8 cycles @ 200 MHz, 10 @ 240 MHz). */
#define BIOS_SAMPLE_DELAY_NS     40
#define BIOS_SAMPLE_DELAY_CYCLES ICE_NS_TO_CYCLES(BIOS_SAMPLE_DELAY_NS)
/* BIOS: the "strobe still low?" check comes this much after the sample
 * (puts it ~80 ns after the edge: drops the 8-56 ns non-ROM strobes, ROM
 * reads are 128-136 ns).  Sniffer and writer use the same point. */
#define BIOS_CHECK_AFTER_NS      25
#define BIOS_CHECK_CYCLES        ICE_NS_TO_CYCLES(BIOS_CHECK_AFTER_NS)

/* Mechacon bus sample point after the A (CDDVD OE) falling edge.
 * Measured on a real console (DSLogic 100 MS/s): A low 260-320 ns, B/G/H/I
 * change 30-70 ns after A falls and then hold past the rising edge.
 * 85 ns + ~15 ns sync/wait = ~100 ns: clean on all but 6 of 108780
 * captured strobes (17 cycles @ 200 MHz, 20 @ 240 MHz). */
#define MECH_SAMPLE_DELAY_NS     85
#define MECH_SAMPLE_DELAY_CYCLES ICE_NS_TO_CYCLES(MECH_SAMPLE_DELAY_NS)

/* DIRECT writer: when a patch nibble goes onto B,G,H,I after A falls.
 * Default = the read point (as the asm: detect the edge, then write).
 * Smaller (e.g. 10) puts the Pico's value on the bus before the drive-side
 * chip starts driving (30-70 ns) - an experiment for the "drive won't
 * unlock" case.  Below 60 ns the glitch gate cannot work and is dropped
 * for the writer (no A glitches were seen inside patch windows). */
#ifndef MECH_WRITE_DELAY_NS
#define MECH_WRITE_DELAY_NS      MECH_SAMPLE_DELAY_NS
#endif
#define MECH_WRITE_DELAY_CYCLES  ICE_NS_TO_CYCLES(MECH_WRITE_DELAY_NS)

/* Mechacon 1-byte patch (ALL_CDDVD_PATCH1 / CDDVD_REGION): the asm resets
 * /1R in ALL_CDDVD_PATCH1, so I,B are driven from the next A edge until the
 * edge after it (one whole strobe period, read by the mechacon at A rising). */

/* MECH_PATCH 0: test build that never drives the mechacon bus - tells
 * mechacon-patch faults apart from BIOS-patch faults.  Edit here (VS Code)
 * or pass cmake -DICE_MECH_PATCH=OFF.  The banner prints the value in use. */
#ifndef MECH_PATCH
#define MECH_PATCH 1
#endif

/* Debug read-back of mechacon patches (DIRECT): sample B,G,H,I this long
 * after A falls, before A rises (measured A low 260-320 ns). */
#define MECH_READBACK_NS         230

/* Mechacon OE "A": 1 = active low (latch the falling edge, data valid while
 * low) - this is what the original flip-flop wiring behaves like. */
#define MECH_OE_ACTIVE_LOW       1

/* DIRECT mode: which A pulses are mechacon strobes.
 *   1 (default) = only pulses still low at the sample point (~100 ns).
 *       Tested: PS2 DVD unlock and the PS1-mode v1-v8 sync.
 *   0 = every pulse >= MECH_MIN_PULSE_NS (~20 ns), like a 74HC73 clock
 *       input - the short pulses between strobes count too.  With this the
 *       PS1-mode v1-v8 sync did not complete in testing.
 * FLIPFLOP builds use the real 74HC73 and ignore this. */
#ifndef MECH_GLITCH_FILTER
#define MECH_GLITCH_FILTER 1
#endif
#define MECH_MIN_PULSE_NS        20
#define MECH_MIN_PULSE_CYCLES    ICE_NS_TO_CYCLES(MECH_MIN_PULSE_NS)
#if MECH_GLITCH_FILTER && MECH_OE_ACTIVE_LOW && !defined(MECH_OE_FLIPFLOP)
#define MECH_GLITCH_ACTIVE 1
#else
#define MECH_GLITCH_ACTIVE 0
#endif
#if !MECH_OE_ACTIVE_LOW && !defined(MECH_OE_FLIPFLOP)
#error "DIRECT strobe gates need A active low (JMP pin = A high)"
#endif

#if BIOS_SAMPLE_DELAY_CYCLES > 31 || MECH_SAMPLE_DELAY_CYCLES > 31
#error "PIO sample delays must be 0..31 cycles at this clock - lower the ns values"
#endif
#if BIOS_CHECK_CYCLES < 2 || BIOS_CHECK_CYCLES > 32
#error "BIOS_CHECK_AFTER_NS out of range for this clock"
#endif


/* SCEx timing: each SCEX_HI/SCEX_LOW cell is ~4.0 ms on the 50 MHz SX28
 * (59 * 212 * 16 cycles = 250 bps).  During SCEX_HI the SX28 executed
 * `not ra` every outer loop (59 times) which toggles the S line every
 * ~68 us - kept for fidelity (the original FINAL disassembly does it too). */
#define SCEX_CELL_US             4000
#define SCEX_HI_TOGGLES          59

/* Timeouts that were busy-loop counters on the SX28 (50 MHz, 6 cycles/iter):
 *  PS2LOGO sync wait  : 0x50*255*255*120ns  ~= 0.62 s of BIOS-idle (CE high)
 *                       time -> auto-reset (RSTBUMP)
 *  xcdvdman reload    : 200*255^3*120ns     ~= 400 s of BIOS-idle (CE high)
 *                       time -> sleep (CS-low time does not count, as loopx) */
#define PS2LOGO_SYNC_TIMEOUT_US   625000u

/* Console standby = BIOS /CE and /OE both low (main rail off) this long;
 * the Pico then restarts as the SX28 did on power-up (console_io.c). */
#ifndef ICE_STANDBY_MS
#define ICE_STANDBY_MS            1000u
#endif
#define XCDVDMAN_RELOAD_TIMEOUT_MS 400000u

#define MODE_SELECT_PS1_WINDOW_100MS   10   /* hold reset 1 s -> PS1 mode     */
#define MODE_SELECT_DEBOUNCE_100MS      5
#define MODE_SELECT_DEV1_WINDOW_100MS 100   /* re-tap within 10 s -> DEV1     */

#endif /* ICE_CONFIG_H */
