/*
 * yd_rp2040.h - Pico SDK board header for the VCC-GND Studio YD-RP2040.
 *               Used automatically when building with -DICE_BOARD=yd_rp2040.
 *
 * >>> SET YOUR FLASH SIZE HERE (MB): 4 or 16 (also 2/8 if you have one) <<<
 * The chip is printed on the flash IC next to the RP2040:
 *   W25Q32 = 4 MB, W25Q128 = 16 MB.
 * You can also override it from CMake: -DYD_RP2040_FLASH_MB=4
 */
// pico_cmake_set PICO_PLATFORM=rp2040

#ifndef _BOARDS_YD_RP2040_H
#define _BOARDS_YD_RP2040_H

#ifndef YD_RP2040_FLASH_MB
#define YD_RP2040_FLASH_MB 16
#endif

// For board detection
#define YD_RP2040

// --- LED ---
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif
// WS2812 on GP23 is only connected when the R68 pads are bridged
#ifndef PICO_DEFAULT_WS2812_PIN
#define PICO_DEFAULT_WS2812_PIN 23
#endif

// --- UART (not used by this firmware) ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// --- FLASH ---
// W25Q32JV / W25Q128JV both work with the W25Q080 second stage
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (YD_RP2040_FLASH_MB * 1024 * 1024)
#endif

// All boards have B1 RP2040
#ifndef PICO_RP2040_B0_SUPPORTED
#define PICO_RP2040_B0_SUPPORTED 0
#endif

#endif
