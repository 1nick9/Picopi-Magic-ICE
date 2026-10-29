#include "scex.h"
#include "console_io.h"
#include "pico/time.h"
#include "config.h"
#include "state.h"
#include "hardware/gpio.h"
#include "bios_bus.h"
#include "pico/time.h"

/* SCEx_DATA: "SCEA" (USA, 0) "SCEI" (JAP, 4) "SCEE" (UK, 8) */
static const char scex_data[12] = { 'S','C','E','A', 'S','C','E','I', 'S','C','E','E' };

void scex_init(void)
{
    gpio_init(SCEX_PIN);
    gpio_set_dir(SCEX_PIN, GPIO_IN);     /* mov !ra,#$f : S is input when idle */
    gpio_disable_pulls(SCEX_PIN);
    gpio_put(SCEX_PIN, 0);
}

static void cs_poll(void)
{
    /* SCEX_LOW: "snb IO_BIOS_CS ; jmp :loop2 ; setb SCEX_FLAG" - the CS
     * level, once per outer loop (59 x per 4 ms cell), as in the asm */
    if (bios_ce_low()) st.scex_flag = true;
}

/* cell timing; the SX28 interrupt can hit here as anywhere else */
static void scex_wait_us(uint32_t us)
{
    uint32_t t0 = time_us_32();
    while (time_us_32() - t0 < us) ice_isr_poll();
}

/* SCEX_HI: S high, then `not ra` every ~68 us for 59 iterations (4 ms). */
static void scex_hi(void)
{
    gpio_put(SCEX_PIN, 1);
    for (int i = 0; i < SCEX_HI_TOGGLES; i++) {
        gpio_xor_mask(1u << SCEX_PIN);
        scex_wait_us(SCEX_CELL_US / SCEX_HI_TOGGLES);
    }
}

/* SCEX_LOW: S low for 4 ms, sampling BIOS CE on the way. */
static void scex_low(void)
{
    gpio_put(SCEX_PIN, 0);
    for (int i = 0; i < SCEX_HI_TOGGLES; i++) {
        cs_poll();
        scex_wait_us(SCEX_CELL_US / SCEX_HI_TOGGLES);
    }
}

void scex_send(void)
{
    int off = st.jap_flag ? 4 : (st.uk_flag ? 8 : 0);

    gpio_set_dir(SCEX_PIN, GPIO_OUT);     /* mov !ra,#$b */

    for (int n = 0; n < 4; n++, off++) {
        uint8_t byte = (uint8_t)~scex_data[off];   /* not VAR_PSX_BYTE */
        scex_low();
        scex_low();
        scex_hi();
        for (int bit = 0; bit < 8; bit++) {        /* rr: LSB first */
            if (byte & 1) scex_hi(); else scex_low();
            byte >>= 1;
        }
    }

    gpio_put(SCEX_PIN, 0);
    for (int i = 0; i < 22; i++) scex_low();

    gpio_set_dir(SCEX_PIN, GPIO_IN);      /* mov !ra,#$f */
}
