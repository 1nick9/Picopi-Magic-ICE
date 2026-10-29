/*
 * scex.h - PS1 SCEx injection on pad S (SEND_SCEX / SCEX_HI / SCEX_LOW).
 */
#ifndef ICE_SCEX_H
#define ICE_SCEX_H

#include <stdbool.h>

void scex_init(void);

/* SEND_SCEX: one full SCEx burst (4 x "SCE?" for the console region, then
 * 22 low cells).  Sets st.scex_flag if BIOS CE was seen low while sending
 * (the asm sampled IO_BIOS_CS inside SCEX_LOW). */
void scex_send(void);

#endif
