/*
 * ice_board_yd_rp2040.h - pin map for the VCC-GND Studio YD-RP2040.
 *                         Selected with -DICE_BOARD=yd_rp2040.
 *
 * The YD-RP2040 has the Pico header layout (GP0..GP22, GP26..GP28), so the
 * pin map is the same as the Pico one.  On-board parts kept clear of:
 *   GP23  WS2812 (only if the R68 pads are bridged)
 *   GP24  USR key
 *   GP25  blue LED  -> used as STATUS_LED_PIN
 *
 * FLASH SIZE: set it in boards/yd_rp2040.h (YD_RP2040_FLASH_MB = 4 or 16).
 */
#ifndef ICE_BOARD_PINS_H
#define ICE_BOARD_PINS_H

#define ICE_BOARD_NAME   "YD-RP2040"

#define BIOS_D0_PIN      0      /* D0..D7 = GP0..GP7 */
#define BIOS_CE_PIN      8
#define BIOS_OE_PIN      9

#define RESET_PIN        10
#define EJECT_PIN        11
#define SCEX_PIN         12

#define MECH_F_PIN       13     /* F: jumper input only (not in the PIO)  */
#define MECH_B_PIN       14     /* B,G,H,I = 4 consecutive GPIOs (PIO)    */

#define MECH_A_PIN       18     /* DIRECT   */
#define MECH_FF_Q_PIN    18     /* FLIPFLOP */
#define MECH_FF_R_PIN    19     /* FLIPFLOP */

#define STATUS_LED_PIN   25

#endif
