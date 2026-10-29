/*
 * bios_ident.c - BIOS_GET_SYNC .. RESTDOWN_CHK_PS2MODEorOTHER
 *
 * Watches the boot ROM reads for the version string
 *      P S 2 0 1 r 0 R ... 0 0 y
 *      0 1 2 3 4 5 6 7       A B C
 * r = kernel revision digit ('1' 1.1x, '2' 1.2x, '5' 1.5x, '7' 1.7x,
 *     '9' 1.9x, '0' 2.0x, '2' 2.2x), R = region letter, y = year digit.
 */
#include "routines.h"
#include "bios_bus.h"
#include "mech_bus.h"
#include "console_io.h"
#include "config.h"
#include "debug_log.h"

uint8_t   sram[SRAM_MAX];
size_t    sram_len;
uint8_t   logo_repeat;
ice_state_t st;

void sram_load(const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n && i < SRAM_MAX; i++) sram[i] = data[i];
    sram_len = n;
}

static uint8_t s_stream[SRAM_MAX + 1];

void bios_patch_sram_arm(uint8_t preload, size_t k, size_t count)
{
    s_stream[0] = preload;
    for (size_t i = 0; i < count; i++) s_stream[1 + i] = sram[k + i];
    bios_out_arm(s_stream, count + 1);
}

void bios_patch_sram(uint8_t preload, size_t k, size_t count)
{
    bios_patch_sram_arm(preload, k, count);
    bios_out_fire();
    bios_out_finish("sram");
}

ice_st_t __not_in_flash_func(bios_get_sync)(void)
{
    bios_flush();
    DBG("BIOS_GET_SYNC: waiting for \"PS20\" on the ROM bus");

    /* "P" "S" "2" "0" on consecutive ROM reads (BIOS_GET_SYNC) */
    bios_sync(BIOS_SEQ4('P', 'S', '2', '0'), 4);

    /* CAPTURE_BIOS_REV: skip byte4 ("x" of x.00), take byte5 */
    bios_skip(1);
    st.bios_rev = bios_byte();

    /* CAPTURE_BIOS_REGION: scan for a '0', then take the next byte */
    bios_wait_byte('0');
    st.bios_region = bios_byte();

    /* CHECK_BYTE_AB_REGION_CAPTURE_YR: scan for '0','0', take next */
    bios_sync(BIOS_SEQ2('0', '0'), 2);
    st.bios_yr = bios_byte();

    DBG("BIOS ident: rev '%c' (%02x)  region '%c' (%02x)",
        DBG_CH(st.bios_rev), st.bios_rev, DBG_CH(st.bios_region), st.bios_region);
    DBG("BIOS ident: year digit '%c' (%02x)%s", DBG_CH(st.bios_yr), st.bios_yr,
        st.bios_yr == '2' ? " -> 2002 / v7: logo patched twice" : "");
    if (bios_overflow_check())
        DBG("!! sniffer FIFO overflowed during ident - values may be wrong");

    switch (st.bios_region) {
    case 'A':                       /* BIOS_USA */
        st.usa_flag = true;
        break;
    case 'W': {                     /* BIOS_V14: 'W' = v14 / 75k+ decka */
        st.v14_flag = true;
#if H2O75KJMPERS
        /* clrb IO_CDDVD_OE_A_1R ; sb IO_CDDVD_BUS_f ; jmp BIOS_USA
         * F pad pulled low by the jumper -> USA v14 */
        if (!mech_pin_f()) { st.usa_flag = true; DBG("v14: F jumper LOW -> USA"); break; }
#endif
#if defined(USAv14)
        st.usa_flag = true; break;
#elif defined(JAPv14orv8)
        st.jap_flag = true; break;
#else
        st.uk_flag = true;          /* falls into BIOS_UK */
        break;
#endif
    }
    case 'E':                       /* europe */
    case 'R':                       /* russia (PAL) */
    case 'C':                       /* china (PAL region, NTSC-C) */
        st.uk_flag = true;
        break;
    default:                        /* BIOS_JAP */
        st.jap_flag = true;
        break;
    }

    DBG("region flags: usa=%u uk(pal)=%u jap=%u v14=%u",
        st.usa_flag, st.uk_flag, st.jap_flag, st.v14_flag);

    /* RESTDOWN_CHK_PS2MODEorOTHER:
     *     snb IO_REST ; jmp TAP_BOOT_MODE
     * IO_REST high (= reset NOT pressed, normal power-on) -> PS2 mode.
     * Only a reset held through power-on goes on to the macrovision patch
     * and MODE_SELECT (PS1 / DEV1 / disable). */
    if (!reset_is_active()) {
        DBG("RESTDOWN_CHK: reset released -> PS2 mode (TAP_BOOT_MODE)");
        return ST_TAP_BOOT_MODE;
    }
    DBG("RESTDOWN_CHK: reset held at power-on -> macro patch + MODE_SELECT");
    return ST_MACRO_CHECK;
}
