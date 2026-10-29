/*
 * main.c - Magic ICE Final / H2O-Improved on an RP2040
 *
 * STARTUP + the inter-page jump web of the SX28 source, as a state machine.
 * Each routine in routines.h returns the label it would have jumped to.
 *
 * ice_main() always runs on core 1.
 * ICE_DEBUG=0 : core 0 only runs the console standby watch.
 * ICE_DEBUG=1 : core 0 runs USB + the debug printer (debug_log.c) + the
 *               standby watch, so USB interrupts never disturb the
 *               bus-timing code.
 */
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

#include "config.h"
#include "state.h"
#include "routines.h"
#include "bios_bus.h"
#include "mech_bus.h"
#include "scex.h"
#include "console_io.h"
#include "debug_log.h"

volatile bool ice_power_on_phase = true;

static const char *const s_state_names[] = {
    [ST_CLEAR_CONSOLE_INFO]     = "CLEAR_CONSOLE_INFO / BIOS_GET_SYNC",
    [ST_MACRO_CHECK]            = "MACRO_CHECK_IF_V9to14",
    [ST_MODE_SELECT]            = "MODE_SELECT_START",
    [ST_TAP_BOOT_MODE]          = "TAP_BOOT_MODE",
    [ST_HOLD_BOOT_MODES]        = "HOLD_BOOT_MODES",
    [ST_PS2_MODE_START]         = "PS2_MODE_START",
    [ST_PS2_CHECK_VERSION]      = "PS2_CHECK_IF_V1_v2or3_V4_V5to8 (dev1)",
    [ST_CHECK_IF_START_PS2LOGO] = "CHECK_IF_START_PS2LOGO",
    [ST_TRAY_IS_EJECTED]        = "TRAY_IS_EJECTED",
    [ST_START_CDDVD_PATCH]      = "START_CDDVD_PATCH (mechacon)",
    [ST_PS1_MODE_START_PATCH]   = "PS1_MODE_START_PATCH (SCEx)",
    [ST_PS1_LOGO_PATCH]         = "PS1_LOGO_PATCH",
    [ST_PS1_CONSOLE_PAL_YFIX]   = "PS1_CONSOLE_PAL_YFIX",
    [ST_PS2LOGO_LOOPZ]          = "PS2_PS2LOGO:loopz",
    [ST_PS2LOGO_BACK]           = "PS2_PS2LOGO:back",
    [ST_IS_XCDVDMANX]           = "IS_XCDVDMANX",
    [ST_IS_XCDVDMAN]            = "IS_XCDVDMAN",
    [ST_DEV1_MODE_LOAD_START]   = "DEV1_MODE_LOAD_START",
    [ST_FINISHED_RUN_START]     = "FINISHED_RUN_START",
    [ST_FINISHED_RUN_START_P2]  = "FINISHED_RUN_START_P2",
    [ST_SLEEP]                  = "SLEEP (PS2/PS1_MODE_RB_IO_SET_SLEEP)",
    [ST_SLEEP_FOREVER]          = "DISABLE_MODE (sleep until standby)",
    [ST_ISR_SLEEP]              = "interrupt: sleep until RESET release / tray close",
};

const char *ice_state_name(int s)
{
    if (s < 0 || s >= (int)(sizeof s_state_names / sizeof s_state_names[0]) ||
        !s_state_names[s])
        return "?";
    return s_state_names[s];
}

static ice_st_t startup_dispatch(wake_reason_t why)
{
    /* "execute correct startup..." */
    switch (why) {
    case WAKE_POWER_ON: return ST_CLEAR_CONSOLE_INFO;      /* snb pd */
    case WAKE_RESET:    return ST_TAP_BOOT_MODE;           /* WKPND.2 */
    default: break;
    }
    if (tray_is_open())        return ST_TRAY_IS_EJECTED;  /* snb IO_EJECT */
    if (why == WAKE_BIOS_CS)   return ST_IS_XCDVDMANX;     /* WKPND.1 */
    return ST_SLEEP;                                        /* PS2_MODE_RB_IO_SET_SLEEP */
}

static void status_led(bool on)
{
#if STATUS_LED_PIN >= 0
    gpio_put(STATUS_LED_PIN, on);
#else
    (void)on;
#endif
}

static void ice_main(void)
{
#if STATUS_LED_PIN >= 0
    gpio_init(STATUS_LED_PIN);
    gpio_set_dir(STATUS_LED_PIN, GPIO_OUT);
#endif
    status_led(true);

    console_io_init();
    scex_init();
    bios_bus_init();
    mech_bus_init();

    DBG("ice_main up on core %u, %s", get_core_num(), ICE_BOARD_NAME);

    static ice_st_t s;
    static ice_st_t prev;
    s = startup_dispatch(WAKE_POWER_ON);
    prev = (ice_st_t)-1;

    /* the SX28 interrupt (after SET_INTRPT) lands here from any wait loop */
    if (setjmp(ice_isr_jmp)) {
        s = ST_ISR_SLEEP;
        prev = (ice_st_t)-1;
    }

    for (;;) {
        if (s != prev) {
            /* power-on / mode select: RESET may be held for seconds on purpose */
            ice_power_on_phase = (s == ST_CLEAR_CONSOLE_INFO || s == ST_MACRO_CHECK ||
                                  s == ST_MODE_SELECT);
            dbg_state((int)s);
            /* LED: on while working, off while asleep / disabled */
            status_led(s != ST_SLEEP && s != ST_SLEEP_FOREVER);
            prev = s;
        }

        switch (s) {

        case ST_CLEAR_CONSOLE_INFO:            /* CLEAR_CONSOLE_INFO_PREFIND */
            state_clear_console_info();
            logo_repeat = 1;
            s = bios_get_sync();
            break;

        case ST_MACRO_CHECK:
            s = macro_check_if_v9to14();
            break;

        case ST_MODE_SELECT:
            s = mode_select_start();
            break;

        case ST_TAP_BOOT_MODE:                 /* TAP_BOOT_MODE */
            st.psx_flag = false;
            s = ST_HOLD_BOOT_MODES;
            break;

        case ST_HOLD_BOOT_MODES:               /* HOLD_BOOT_MODES */
            if (st.dev1_flag) { s = ST_DEV1_MODE_LOAD_START; break; }
            st.soft_rst     = true;
            st.ej_flag      = false;
            st.x_flag       = true;
            st.v12logo_flag = false;
            DBG("boot mode: %s", st.psx_flag ? "PS1" : "PS2");
            s = ST_PS2_MODE_START;
            break;

        case ST_PS2_MODE_START:
            s = ps2_mode_start(false);
            break;

        case ST_PS2_CHECK_VERSION:             /* dev1 entry */
            s = ps2_mode_start(true);
            break;

        case ST_CHECK_IF_START_PS2LOGO:
            s = check_if_start_ps2logo();
            break;

        case ST_TRAY_IS_EJECTED:
            s = tray_is_ejected();
            break;

        case ST_START_CDDVD_PATCH:
            s = start_cddvd_patch();
            break;

        case ST_PS1_MODE_START_PATCH:
            s = ps1_mode_start_patch();
            break;

        case ST_PS1_LOGO_PATCH:
            s = ps1_logo_patch();
            break;

        case ST_PS1_CONSOLE_PAL_YFIX:
            s = ps1_console_pal_yfix();
            break;

        case ST_PS2LOGO_LOOPZ:
            s = ps2logo_loopz();
            break;

        case ST_PS2LOGO_BACK:
            s = ps2logo_back();
            break;

        case ST_IS_XCDVDMANX:
            s = is_xcdvdman(true);
            break;

        case ST_IS_XCDVDMAN:
            s = is_xcdvdman(false);
            break;

        case ST_DEV1_MODE_LOAD_START:
            s = dev1_mode_load_start();
            break;

        case ST_FINISHED_RUN_START:
            s = finished_run_start(true);
            break;

        case ST_FINISHED_RUN_START_P2:
            s = finished_run_start(false);
            break;

        case ST_SLEEP: {                       /* PS2/PS1_MODE_RB_IO_SET_SLEEP */
            wake_reason_t why = console_sleep(st.psx_flag);
            static const char *const wn[] = { "power-on", "RESET", "EJECT", "BIOS CE" };
            DBG("woke up: %s", wn[why]);
            s = startup_dispatch(why);
            break;
        }

        case ST_ISR_SLEEP: {                   /* interrupt at $0000 */
            wake_reason_t why = console_isr_sleep();
            DBG("woke up: %s", why == WAKE_RESET ? "RESET released" : "tray closed");
            s = startup_dispatch(why);
            break;
        }

        case ST_SLEEP_FOREVER:                 /* DISABLE_MODE */
        default:
            console_disabled_until_standby();
        }
    }
}

int main(void)
{
#if SYS_CLOCK_KHZ > 200000
    /* overclock like fruitchip: raise the core voltage first, let it settle */
    vreg_set_voltage(SYS_VREG_VOLTAGE);
    busy_wait_us_32(1000);
#endif
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);   /* panics if not reachable */

#if ICE_DEBUG
    dbg_run(ice_main);                         /* never returns; core 0 also
                                                * runs the standby watch */
#else
    /* core 1: the modchip.  core 0: console standby watch only. */
    static uint32_t core1_stack[8192 / 4];
    multicore_launch_core1_with_stack(ice_main, core1_stack, sizeof core1_stack);
    for (;;) {
        console_standby_watch();
        busy_wait_us_32(500);
    }
#endif
    return 0;
}
