/*
 * pio_patch.h - load-time fix-ups for pioasm programs.
 *
 * pioasm needs constant GPIO numbers in "wait <pol> gpio <n>", but the pin
 * map comes from the board header.  Instead of hard-coding pin numbers in
 * the .pio files (and keeping two copies in sync), the programs are written
 * with "gpio 0" and copied to RAM here with:
 *   - every "wait <pol> gpio <n>" pointed at `trigger_gpio`
 *   - the delay of the instruction at `delay_at` set to `delay`
 *   - optionally, the instruction at `nop_at` replaced with a NOP
 * The patched copy is then loaded with pio_add_program() as usual.
 */
#ifndef ICE_PIO_PATCH_H
#define ICE_PIO_PATCH_H

#include <stdint.h>
#include "hardware/pio.h"

#define PIO_PATCH_MAX 32
#define PIO_PATCH_NONE (-1)

typedef struct {
    uint16_t      insn[PIO_PATCH_MAX];
    pio_program_t prog;
} pio_patched_t;

/* WAIT: 001 ddddd p ss iiiii  (ss = 00 -> GPIO) */
#define PIO_INSN_IS_WAIT_GPIO(i)  (((i) & 0xe060u) == 0x2000u)

static inline const pio_program_t *pio_patch(pio_patched_t *dst,
                                             const pio_program_t *src,
                                             uint trigger_gpio,
                                             int delay_at, uint delay,
                                             int nop_at)
{
    for (uint i = 0; i < src->length && i < PIO_PATCH_MAX; i++) {
        uint16_t ins = src->instructions[i];
        if (PIO_INSN_IS_WAIT_GPIO(ins))
            ins = (uint16_t)((ins & ~0x1fu) | (trigger_gpio & 0x1fu));
        if ((int)i == delay_at)
            ins = (uint16_t)((ins & ~0x1f00u) | ((delay & 0x1fu) << 8));
        if ((int)i == nop_at)
            ins = (uint16_t)pio_encode_nop();
        dst->insn[i] = ins;
    }
    dst->prog = *src;
    dst->prog.instructions = dst->insn;
    return &dst->prog;
}

/* set the delay field of one more instruction of an already patched copy */
static inline void pio_patch_delay(pio_patched_t *p, uint offset, uint delay)
{
    p->insn[offset] = (uint16_t)((p->insn[offset] & ~0x1f00u) | ((delay & 0x1fu) << 8));
}

#endif
