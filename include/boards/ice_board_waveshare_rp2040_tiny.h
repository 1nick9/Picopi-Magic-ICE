/*
 * ice_board_waveshare_rp2040_tiny.h - pin map for the Waveshare RP2040-Tiny.
 *                                     Selected with -DICE_BOARD=waveshare_rp2040_tiny.
 *
 * The Tiny brings out 20 GPIOs on its edge pads: GP0..GP15 and GP26..GP29
 * (GP16 drives the on-board WS2812, GP17..GP25 are not brought out).
 * The port needs 19 (DIRECT) or 20 (FLIPFLOP), so every pad is used:
 *
 *   Magic ICE pad   SX28 pin   Tiny pad
 *   V..M (D0-D7)    rc.0-7     GP0..GP7     8 consecutive (PIO data)
 *   W   (BIOS CE)   rb.1       GP8          separate trigger input
 *   R   (BIOS OE)   ra.0       GP9          separate trigger input
 *   F (jumper)      rb.0       GP10         plain GPIO + pull-up, not in the PIO
 *   B,G,H,I         rb.4-7     GP11..GP14   4 consecutive (PIO data)
 *   A   (CDDVD OE)  ff CLK     GP15         DIRECT
 *   /1Q             ra.1       GP15         FLIPFLOP
 *   RST             rb.2       GP26
 *   Z   (eject)     rb.3       GP27
 *   S   (SCEx)      ra.2       GP28
 *   /1R             ra.3       GP29         FLIPFLOP only (free in DIRECT)
 *
 * Check the pad labels on your board against Waveshare's pinout picture
 * before soldering - only the GPIO numbers matter to the firmware.
 * GP0/GP1 are the SDK's default UART pins; UART stdio is never enabled in
 * this project (debug goes over USB), so using them for D0/D1 is fine.
 */
#ifndef ICE_BOARD_PINS_H
#define ICE_BOARD_PINS_H

#define ICE_BOARD_NAME   "Waveshare RP2040-Tiny"

#define BIOS_D0_PIN      0      /* D0..D7 = GP0..GP7 */
#define BIOS_CE_PIN      8
#define BIOS_OE_PIN      9

#define MECH_F_PIN       10     /* F: jumper input only (not in the PIO)  */
#define MECH_B_PIN       11     /* B,G,H,I = 4 consecutive GPIOs (PIO)    */

#define MECH_A_PIN       15     /* DIRECT   */
#define MECH_FF_Q_PIN    15     /* FLIPFLOP */
#define MECH_FF_R_PIN    29     /* FLIPFLOP */

#define RESET_PIN        26
#define EJECT_PIN        27
#define SCEX_PIN         28

/* Only a WS2812 on GP16 - no plain LED to toggle. */
#define STATUS_LED_PIN   (-1)

#endif
