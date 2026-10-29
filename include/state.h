/*
 * state.h - the SX28 register file, as a struct.
 *
 * VAR_PATCH_FLAGS / VAR_SWITCH bits become bools; VAR_BIOS_* become bytes.
 * The DCx delay/scratch counters are replaced by local variables in the
 * routines that used them.  The SRAM "patch cache" ($15.. with the OR $10
 * banking trick) becomes a plain byte array, see patch_tables.h.
 */
#ifndef ICE_STATE_H
#define ICE_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* VAR_PATCH_FLAGS */
    bool ej_flag;        /* bit0: eject routine done / skip logo patch       */
    bool soft_rst;       /* bit1: soft reset - media patch may repeat        */
    bool psx_flag;       /* bit2: PS1 mode                                   */
    bool v10_flag;       /* bit3: 1.9 (v10/v11) kernel, also set for 2.0/2.2 */
    bool uk_flag;        /* bit4: PAL region ('E','R','C')                   */
    bool usa_flag;       /* bit5: NTSC-U ('A')                               */
    bool jap_flag;       /* bit6: NTSC-J (fallthrough)                       */
    bool scex_flag;      /* bit7: BIOS CE seen low while sending SCEx        */

    /* VAR_SWITCH */
    bool v12_flag;       /* bit0: 2.0 BIOS (v12)                             */
    bool v12logo_flag;   /* bit1: PS1 v12 logo patch pending                 */
    bool jap_v8;         /* bit2: JAP v8 with last SPC mechacon (dragon)     */
    bool x_flag;         /* bit3: first xcdvdman patch                       */
    bool dev1_flag;      /* bit4: DEV1 mode                                  */
    bool v14_flag;       /* bit5: 'W' region = v14 / 75k+ decka              */
    bool v0_flag;        /* bit6: unused (10k-18k)                           */

    /* VAR_BIOS_* */
    uint8_t bios_rev;    /* byte5 of "PS201?0?C200?" : '1','2','5','7','9','0','2' */
    uint8_t bios_region; /* byte7 : 'A','W','E','R','C', else JAP            */
    uint8_t bios_yr;     /* byteC : last digit of the year ('2' = 2002 = v7) */
} ice_state_t;

extern ice_state_t st;

/* Power-up reason, replaces the SX28 wake-up pending bits (WKPND_B). */
typedef enum {
    WAKE_POWER_ON = 0,   /* console came out of standby: full BIOS ident     */
    WAKE_RESET,          /* reset edge while sleeping                        */
    WAKE_EJECT,          /* tray lid edge while sleeping                     */
    WAKE_BIOS_CS,        /* BIOS CE edge while sleeping (xcdvdman reload)    */
} wake_reason_t;

static inline void state_clear_console_info(void)
{
    ice_state_t z = {0};
    st = z;
}

#endif
