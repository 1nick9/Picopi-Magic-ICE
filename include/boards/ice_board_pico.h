/*
 * ice_board_pico.h - pin map for the Raspberry Pi Pico (and pin-compatible
 *                    Pico clones).  Selected with -DICE_BOARD=pico.
 *
 * Rules (checked in config.h):
 *   - BIOS D0..D7 : 8 consecutive GPIOs          (PIO "in pins, 8" / "out pins, 8")
 *   - Mech B,G,H,I : 4 consecutive GPIOs         (PIO in/out pins)
 *   - Mech F (jumper input) : any free GPIO
 *   - BIOS CE, BIOS OE, Mech A (or /1Q, /1R) : ANY free GPIO, they are
 *     separate trigger inputs and are never part of the data word.
 *
 *   Magic ICE pad   SX28 pin   Pico GPIO
 *   V..M (D0-D7)    rc.0-7     GP0..GP7
 *   W   (BIOS CE)   rb.1       GP8
 *   R   (BIOS OE)   ra.0       GP9
 *   RST             rb.2       GP10
 *   Z   (eject)     rb.3       GP11
 *   S   (SCEx)      ra.2       GP12
 *   F (jumper)      rb.0       GP13   plain GPIO + pull-up, not in the PIO
 *   B,G,H,I         rb.4-7     GP14..GP17
 *   A   (CDDVD OE)  ff CLK     GP18   (DIRECT)
 *   /1Q             ra.1       GP18   (FLIPFLOP)
 *   /1R             ra.3       GP19   (FLIPFLOP)
 */
#ifndef ICE_BOARD_PINS_H
#define ICE_BOARD_PINS_H

#define ICE_BOARD_NAME   "Raspberry Pi Pico"

#define BIOS_D0_PIN      0      /* D0..D7 = GP0..GP7 */
#define BIOS_CE_PIN      8
#define BIOS_OE_PIN      9

#define RESET_PIN        10
#define EJECT_PIN        11
#define SCEX_PIN         12

#define MECH_F_PIN       13     /* F: jumper input only (not in the PIO)  */
#define MECH_B_PIN       14     /* B,G,H,I = 4 consecutive GPIOs (PIO)    */

#define MECH_A_PIN       18     /* DIRECT   : mechacon CD/DVD OE           */
#define MECH_FF_Q_PIN    18     /* FLIPFLOP : flip-flop /1Q                */
#define MECH_FF_R_PIN    19     /* FLIPFLOP : flip-flop /1R                */

/* On-board LED for a coarse status indication (-1 = none). */
#define STATUS_LED_PIN   25

#endif
