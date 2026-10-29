/*
 * debug_log.h - non-blocking debug events, printed over USB (stdio_usb).
 *
 * Build with -DICE_DEBUG=ON.  Then:
 *   core 1 runs the modchip (all bus work, IRQs for sleep/wake)
 *   core 0 runs the USB stack and prints the events
 * so USB interrupts never land on the core that is chasing the BIOS bus.
 *
 * DBG(fmt, up to 6 integer args) only copies the arguments into a queue
 * (a few dozen cycles, never waits).  The format string is printed later by
 * core 0 with printf, so use %u %x %02x %c etc. with integer arguments, or
 * %s with a string literal / static string (never a stack buffer).
 * If the queue is full the event is dropped and counted.
 *
 * With ICE_DEBUG=0 every DBG() compiles to nothing.
 */
#ifndef ICE_DEBUG_LOG_H
#define ICE_DEBUG_LOG_H

#include <stdint.h>
#include "config.h"

#if ICE_DEBUG

void dbg_event(const char *fmt, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
               uint32_t e, uint32_t f);
void dbg_state(int state);                /* state-machine transition */

/* Starts core 1 with `ice_main` and runs the USB printer on core 0.
 * Never returns. */
void dbg_run(void (*ice_main)(void));

#else

static inline void dbg_event(const char *fmt, uint32_t a, uint32_t b,
                             uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{ (void)fmt; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
static inline void dbg_state(int state) { (void)state; }

#endif

#if defined(ICE_DEBUG_FORMAT_CHECK)
/* build-only: let gcc -Wformat check every DBG() format against its args */
int dbg_format_check(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define DBG(...)  ((void)dbg_format_check(__VA_ARGS__))
#else
/* up to 6 integer / string-literal arguments - more is a compile error */
#define DBG_NARG_(_1, _2, _3, _4, _5, _6, _7, _8, _9, N, ...) N
#define DBG_NARG(...)  DBG_NARG_(__VA_ARGS__, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define DBG(...)                                                              \
    do { _Static_assert(DBG_NARG(__VA_ARGS__) <= 7,                          \
                        "DBG(): at most 6 arguments after the format");       \
         DBG_(__VA_ARGS__, 0, 0, 0, 0, 0, 0, 0); } while (0)
#endif
#define DBG_(fmt, a, b, c, d, e, f, ...)                                      \
    do { if (ICE_DEBUG) dbg_event((fmt), (uint32_t)(a), (uint32_t)(b),        \
                                  (uint32_t)(c), (uint32_t)(d),               \
                                  (uint32_t)(e), (uint32_t)(f)); } while (0)

/* Printable char for %c of a byte that may not be ASCII. */
#define DBG_CH(b)  (((b) >= 0x20 && (b) < 0x7f) ? (b) : '?')

#endif
