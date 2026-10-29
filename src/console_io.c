#include "console_io.h"
#include "config.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/time.h"
#include "debug_log.h"
#include "bios_bus.h"
#include "mech_bus.h"
#include "hardware/watchdog.h"
#include "hardware/pio.h"

volatile bool ice_isr_armed;
jmp_buf       ice_isr_jmp;

void ice_sleep_ms(unsigned ms)
{
    absolute_time_t until = make_timeout_time_ms(ms);
    while (!time_reached(until)) ice_isr_poll();
}

void ice_release_all_outputs(void)
{
    /* BIOS writer: stop, D0-7 back to inputs */
    pio_sm_set_enabled(BIOS_PIO, BIOS_OUT_SM, false);
    pio_sm_set_consecutive_pindirs(BIOS_PIO, BIOS_OUT_SM, BIOS_D0_PIN, 8, false);
    /* mechacon writer: B,G,H,I back to inputs, idle and running */
    pio_sm_set_consecutive_pindirs(MECH_PIO, MECH_OUT_SM, MECH_B_PIN, 4, false);
    mech_out_arm(NULL, 0);
    mech_out_fire();
    gpio_set_dir(SCEX_PIN, GPIO_IN);
    gpio_set_dir(RESET_PIN, GPIO_IN);
}

void ice_isr_enter(void)
{
    ice_isr_armed = false;
    ice_release_all_outputs();
    longjmp(ice_isr_jmp, 1);
}

void console_io_init(void)
{
    gpio_init(RESET_PIN);
    gpio_set_dir(RESET_PIN, GPIO_IN);
    gpio_disable_pulls(RESET_PIN);       /* console pulls it up; RP2040 default is pull-down */
    gpio_put(RESET_PIN, 0);          /* latch 0: driving the pin = pulling low */

    gpio_init(EJECT_PIN);
    gpio_set_dir(EJECT_PIN, GPIO_IN);
    gpio_disable_pulls(EJECT_PIN);
    /* mode $0D: EJECT input as CMOS level (noise); nearest Pico equivalent
     * is Schmitt trigger input, which is the default. */
    gpio_set_input_hysteresis_enabled(EJECT_PIN, true);

    ice_isr_armed = false;          /* power-on: MIWU disabled */
}

bool reset_is_active(void) { return !gpio_get(RESET_PIN); }
bool tray_is_open(void)    { return  gpio_get(EJECT_PIN); }

void console_auto_reset(void)
{
    DBG("AUTORESET: pulling %s low for 100 ms", RSTBUMP ? "RST" : "pad F");
    /* AUTORESET (PS2_PS2LOGO:loop3 timeout) */
#if RSTBUMP
    /* mode $0B: !rb = $ff (wake-up off); rb = 0; !rb = $fb : RST output low */
    console_disarm_wake_edges();
    gpio_put(RESET_PIN, 0);
    gpio_set_dir(RESET_PIN, GPIO_OUT);
    delay_100ms();
    gpio_set_dir(RESET_PIN, GPIO_IN);              /* !rb = $ff */
#else
    /* rb = 0; !rb = $fe : pad F (rb.0) driven low for the 2N7002 gate */
    gpio_init(MECH_F_PIN);
    gpio_put(MECH_F_PIN, 0);
    gpio_set_dir(MECH_F_PIN, GPIO_OUT);
    delay_100ms();
    gpio_set_dir(MECH_F_PIN, GPIO_IN);
#endif
}

void console_arm_wake_edges(void)
{
    /* SET_INTRPT: WKED rb.2 falling / rb.3 rising, pending cleared,
     * WKEN rb.2 + rb.3 */
    ice_edges_clear(RESET_PIN, ICE_EDGE_FALL | ICE_EDGE_RISE);
    ice_edges_clear(EJECT_PIN, ICE_EDGE_FALL | ICE_EDGE_RISE);
    ice_isr_armed = true;
}

void console_disarm_wake_edges(void)
{
    ice_isr_armed = false;
}

/* PS2_MODE_RB_IO_SET_SLEEP: WKED $06 (CS, RESET falling; EJECT rising),
 * pending cleared, WKEN $f1 (CS, RESET, EJECT) or $f3 in PS1 mode. */
wake_reason_t console_sleep(bool psx_mode)
{
    ice_isr_armed = false;
    DBG("sleep: waiting for RESET fall / tray open%s (%s mode)",
        psx_mode ? "" : " / BIOS CE fall", psx_mode ? "PS1" : "PS2");
    ice_edges_clear(RESET_PIN,   ICE_EDGE_FALL | ICE_EDGE_RISE);
    ice_edges_clear(EJECT_PIN,   ICE_EDGE_FALL | ICE_EDGE_RISE);
    ice_edges_clear(BIOS_CE_PIN, ICE_EDGE_FALL | ICE_EDGE_RISE);
    for (;;) {
        /* STARTUP priority: reset pending, then tray level, then CS */
        if (ice_edges(RESET_PIN) & ICE_EDGE_FALL)                return WAKE_RESET;
        if (ice_edges(EJECT_PIN) & ICE_EDGE_RISE)                return WAKE_EJECT;
        if (!psx_mode && (ice_edges(BIOS_CE_PIN) & ICE_EDGE_FALL)) return WAKE_BIOS_CS;
    }
}

/* interrupt at $0000: WKED $08 -> RESET rising (released) or EJECT falling
 * (tray closed), pending cleared, WKEN $f3, sleep */
wake_reason_t console_isr_sleep(void)
{
    bool by_reset = (ice_edges(RESET_PIN) & ICE_EDGE_FALL) != 0;
    DBG("interrupt (%s): sleeping until %s",
        by_reset ? "RESET pressed" : "tray opened", "RESET released / tray closed");
    ice_edges_clear(RESET_PIN, ICE_EDGE_FALL | ICE_EDGE_RISE);
    ice_edges_clear(EJECT_PIN, ICE_EDGE_FALL | ICE_EDGE_RISE);
    /* a tap shorter than the time to get here: already released */
    if (by_reset && !reset_is_active()) return WAKE_RESET;
    for (;;) {
        if (ice_edges(RESET_PIN) & ICE_EDGE_RISE) return WAKE_RESET;
        if (ice_edges(EJECT_PIN) & ICE_EDGE_FALL) return WAKE_EJECT;
    }
}

void console_disabled_until_standby(void)
{
    ice_isr_armed = false;
    DBG("DISABLE_MODE: idle until the console goes to standby (core 0 watch)");
    for (;;) { }                          /* RESET, tray, BIOS all ignored */
}

/* Console standby watch (core 0).  The SX28 lost its supply in standby and
 * restarted from power-on; a Pico on USB or a standby rail does not, so it
 * has to notice standby and reboot.  RESET is not reliable for that (it
 * reads 0 on some consoles and 1 on others while the console is off).  The
 * BIOS ROM's /CE and /OE are: with the main rail on they idle high
 * and only pulse low for a read; with the rail off both sit at 0 V.  So:
 * CE and OE both low, continuously, for ICE_STANDBY_MS = standby.  Not
 * checked during the power-on mode select (RESET held on purpose). */
void console_standby_watch(void)
{
    static bool armed;
    static absolute_time_t low_since;
    bool off = !gpio_get(BIOS_CE_PIN) && !gpio_get(BIOS_OE_PIN);
    if (!off || ice_power_on_phase) {
        armed     = armed || !off;        /* seen the console running */
        low_since = get_absolute_time();
    } else if (armed &&
               absolute_time_diff_us(low_since, get_absolute_time()) >
                   (int64_t)ICE_STANDBY_MS * 1000) {
        DBG("console standby (BIOS CE+OE low %u ms) -> Pico restart = SX28 power-on",
            (unsigned)ICE_STANDBY_MS);
        busy_wait_us_32(20000);
        watchdog_reboot(0, 0, 0);
        for (;;) { }
    }
}
