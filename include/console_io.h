/*
 * console_io.h - reset / eject / jumper pads, auto-reset and sleep.
 *
 * SX28:   IO_REST (rb.2), IO_EJECT (rb.3), MIWU wake-up on port B edges,
 *         the RSTBUMP "AUTORESET" block, DELAY100m.
 */
#ifndef ICE_CONSOLE_IO_H
#define ICE_CONSOLE_IO_H

#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include "state.h"
#include "config.h"
#include "hardware/structs/io_bank0.h"

/* ---- edge latches ------------------------------------------------------
 * The SX28 MIWU latches an edge in WKPND_B whether or not it is asleep.
 * The RP2040 IO_BANK0 raw INTR register does the same for every GPIO
 * (edge bits latch until written back), with no interrupt involved - so
 * every "sleep" and the SET_INTRPT interrupt are done by polling these
 * latches.  Nothing can be lost in an IRQ/WFI hand-over. */
#define ICE_EDGE_FALL 0x4u
#define ICE_EDGE_RISE 0x8u
static inline uint32_t ice_edges(unsigned gpio)
{
    return (io_bank0_hw->intr[gpio >> 3] >> (4u * (gpio & 7u))) & 0xcu;
}
static inline void ice_edges_clear(unsigned gpio, uint32_t ev)
{
    io_bank0_hw->intr[gpio >> 3] = ev << (4u * (gpio & 7u));
}

/* ---- SET_INTRPT / the interrupt at $0000 --------------------------------
 * After SET_INTRPT the SX28 takes an interrupt on RESET falling (button
 * pressed) or EJECT rising (tray opening) from wherever it is, sleeps until
 * RESET rises / the tray closes, and restarts at STARTUP.  Here: armed by
 * console_arm_wake_edges(), checked by ice_isr_poll() in every wait loop
 * (bus FIFO waits, mechacon waits, SCEx cells, delays); a hit releases all
 * outputs and longjmps back to the state machine (state ST_ISR_SLEEP). */
extern volatile bool ice_isr_armed;
/* true until the power-on mode select is over (RESET held on purpose) */
extern volatile bool ice_power_on_phase;
extern jmp_buf       ice_isr_jmp;
void ice_isr_enter(void) __attribute__((noreturn));
static inline void ice_isr_poll(void)
{
    if (ice_isr_armed &&
        ((ice_edges(RESET_PIN) & ICE_EDGE_FALL) || (ice_edges(EJECT_PIN) & ICE_EDGE_RISE)))
        ice_isr_enter();
}

void console_io_init(void);

void ice_sleep_ms(unsigned ms);           /* polls ice_isr_poll() meanwhile */
static inline void delay_100ms(void) { ice_sleep_ms(100); }

bool reset_is_active(void);     /* IO_REST low  (sb/snb IO_REST)  */
bool tray_is_open(void);        /* IO_EJECT high (1 = tray open)   */

/* AUTORESET: pull the console reset (RSTBUMP) - or pad F on the non-RSTBUMP
 * board - low for 100 ms, then release.  Clears flags like the asm does. */
void console_auto_reset(void);

/* PS2_MODE_RB_IO_SET_SLEEP / PS1_MODE_RB_IO_SET_SLEEP:
 * sleep until a falling edge on RESET, EJECT or (PS2 mode only) BIOS CE.
 * Returns the wake reason. */
wake_reason_t console_sleep(bool psx_mode);

/* SET_INTRPT: arm the RESET-fall / EJECT-rise "interrupt" (see above). */
void console_arm_wake_edges(void);
/* the asm disables it again ("mode $0B ; !rb = $ff") - AUTORESET, sleep */
void console_disarm_wake_edges(void);

/* the interrupt's sleep: wait for RESET rising or the tray closing, then
 * the STARTUP dispatch reason (WAKE_RESET / WAKE_EJECT) */
wake_reason_t console_isr_sleep(void);

/* DISABLE_MODE: "sleep chip, can't wake up without put PS2 into stby".
 * Ignores everything; the core-0 standby watch restarts the Pico when the
 * console goes to standby (= the SX28's power-on). */
void console_disabled_until_standby(void) __attribute__((noreturn));

/* release every pin the modchip can drive (BIOS D0-7, mechacon B,G,H,I,
 * SCEx, RESET) - used when the interrupt aborts a routine */
void ice_release_all_outputs(void);

/* standby -> reboot watch, run on core 0 (see console_io.c) */
void console_standby_watch(void);

#endif
