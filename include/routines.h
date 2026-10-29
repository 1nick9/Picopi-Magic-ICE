/*
 * routines.h - the SX28 code pages, as functions.
 *
 * The asm is a web of `jmp`s between pages; here every routine returns the
 * label it would have jumped to, and main() runs the state machine.
 */
#ifndef ICE_ROUTINES_H
#define ICE_ROUTINES_H

#include <stddef.h>
#include <stdint.h>
#include "state.h"

typedef enum {
    ST_CLEAR_CONSOLE_INFO,      /* CLEAR_CONSOLE_INFO_PREFIND -> BIOS_GET_SYNC   */
    ST_MACRO_CHECK,             /* MACRO_CHECK_IF_V9to14 (DVD green/macrovision) */
    ST_MODE_SELECT,             /* MODE_SELECT_START                              */
    ST_TAP_BOOT_MODE,           /* TAP_BOOT_MODE                                  */
    ST_HOLD_BOOT_MODES,         /* HOLD_BOOT_MODES                                */
    ST_PS2_MODE_START,          /* PS2_MODE_START                                 */
    ST_PS2_CHECK_VERSION,       /* PS2_CHECK_IF_V1_v2or3_V4_V5to8 (dev1 entry)    */
    ST_CHECK_IF_START_PS2LOGO,  /* CHECK_IF_START_PS2LOGO                         */
    ST_TRAY_IS_EJECTED,         /* TRAY_IS_EJECTED                                */
    ST_START_CDDVD_PATCH,       /* START_CDDVD_PATCH (mechacon)                   */
    ST_PS1_MODE_START_PATCH,    /* PS1_MODE_START_PATCH (SCEx)                    */
    ST_PS1_LOGO_PATCH,          /* PS1_LOGO_PATCH                                 */
    ST_PS1_CONSOLE_PAL_YFIX,    /* PS1_CONSOLE_PAL_YFIX                           */
    ST_PS2LOGO_LOOPZ,           /* PS2_PS2LOGO:loopz                              */
    ST_PS2LOGO_BACK,            /* PS2_PS2LOGO:back                               */
    ST_IS_XCDVDMANX,            /* IS_XCDVDMANX (SET_INTRPT + IS_XCDVDMAN)        */
    ST_IS_XCDVDMAN,             /* IS_XCDVDMAN                                    */
    ST_DEV1_MODE_LOAD_START,    /* DEV1_MODE_LOAD_START                           */
    ST_FINISHED_RUN_START,      /* FINISHED_RUN_START (dev1 modload repatch)      */
    ST_FINISHED_RUN_START_P2,   /* FINISHED_RUN_START_P2                          */
    ST_SLEEP,                   /* PS2/PS1_MODE_RB_IO_SET_SLEEP                   */
    ST_SLEEP_FOREVER,           /* DISABLE_MODE: sleep until console standby      */
    ST_ISR_SLEEP,               /* interrupt at $0000 (reset / tray after SET_INTRPT) */
} ice_st_t;

/* name of a state, for the debug log */
const char *ice_state_name(int s);

/* ---- "SRAM" patch cache ------------------------------------------------- */
#define SRAM_MAX 136
extern uint8_t sram[SRAM_MAX];
extern size_t  sram_len;
void sram_load(const uint8_t *data, size_t n);

/* [preload] + sram[k .. k+count-1] on consecutive /OE cycles */
void bios_patch_sram_arm(uint8_t preload, size_t k, size_t count);
void bios_patch_sram(uint8_t preload, size_t k, size_t count);

/* persistent counters the asm kept in VAR_DC4 across routines */
extern uint8_t logo_repeat;   /* 2 on 2002 (v7) consoles, else 1 */

/* bios_ident.c */
ice_st_t bios_get_sync(void);              /* BIOS_GET_SYNC .. RESTDOWN_CHK */
/* macro_patch.c */
ice_st_t macro_check_if_v9to14(void);
ice_st_t mode_select_start(void);
/* osdsys_patch.c */
ice_st_t ps2_mode_start(bool skip_psx_check);
void osdsys_sync_and_patch(uint8_t preload, size_t k, size_t n, bool from_p2);
/* ps2logo_patch.c */
ice_st_t check_if_start_ps2logo(void);
ice_st_t ps2logo_loopz(void);
ice_st_t ps2logo_back(void);
ice_st_t ps1drv_patchload_v14(void);       /* used by ps2_mode_start */
ice_st_t ps1_v14_patch(void);              /* PS1_V14_PATCH */
/* xcdvdman.c */
ice_st_t tray_is_ejected(void);
ice_st_t is_xcdvdman(bool set_intrpt);
/* ps1_patch.c */
ice_st_t ps1_mode_start_patch(void);
ice_st_t ps1_logo_patch(void);
ice_st_t ps1_console_pal_yfix(void);
/* dev1.c */
ice_st_t dev1_mode_load_start(void);
ice_st_t finished_run_start(bool set_intrpt);
/* media_patch.c */
ice_st_t start_cddvd_patch(void);

#endif
