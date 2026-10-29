/*
 * patch_tables.h - byte streams extracted from the SX28 `jmp pc+w` tables.
 * Generated into src/patch_tables.c by tools/extract_tables.py.
 *
 * Every OSDSYS array already starts with the "preload" byte that the SX28
 * drove on IO_BIOS_DATA before enabling the outputs, so a whole array is
 * exactly what goes out on consecutive /OE cycles.
 */
#ifndef ICE_PATCH_TABLES_H
#define ICE_PATCH_TABLES_H

#include <stdint.h>

/* OSDSYS kernel patches (PS2 mode), preload included */
extern const uint8_t osd_v1[8];
extern const uint8_t osd_v2v3[8];
extern const uint8_t osd_v4[8];
extern const uint8_t osd_v5v8[24];
extern const uint8_t osd_v9[24];
extern const uint8_t osd_v14[24];
extern const uint8_t osd_v10[24];
extern const uint8_t osd_v12[24];

/* PS1DRV body for v1-v12 PAL Y-fix (preload $3c added by the caller) */
extern const uint8_t ps1drv_data[11];

/* PS2LOGO SRAM images ("PS2_PS2LOGO") */
extern const uint8_t logo_sram_v1v9[110];
extern const uint8_t logo_sram_v10v12[110];
extern const uint8_t logo_sram_v14[123];

/* V14 PS1DRV SRAM image (PS1_V14_PATCH sends 49 bytes from k=6) */
extern const uint8_t ps1drv_v14_sram[55];

/* DEV1 modload patch (bytes 7,8 patched per console by dev1_load()) */
extern const uint8_t dev1_patch_h2o[115];
extern const uint8_t dev1_patch_ice[119];
#if defined(ICEFINALDEV1)
#define DEV1_PATCH_LEN 119
#define dev1_patch     dev1_patch_ice
#else
#define DEV1_PATCH_LEN 115
#define dev1_patch     dev1_patch_h2o
#endif

/* mechacon region patch nibbles: USA @0, PAL @8, JAP @16 */
extern const uint8_t cddvd_patch_data[24];

#endif
