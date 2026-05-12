/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Runtime debug toggle for the DVB engines + library consumers.
 * Off by default; flip on by setting `DVB_DEBUG=1` in the environment
 * before launching the consumer (a tool, your own application).
 *
 * Usage:
 *   DVBDBG("tune fn=%d freq=%u", fn, freq);
 *
 * Resolution:
 *   - First DVBDBG() call reads the env var once and caches the
 *     result in a static int.
 *   - When off, the macro evaluates to a no-op (compiler still
 *     parses the args, but nothing executes / formats).
 *
 * For chip-driver verbosity (si2168/lgdt3306a/mn88472 internal
 * dev_dbg traces) rebuild with `meson configure -Dchip_driver_verbose=true`
 * — that's a separate, much louder switch.
 */

#ifndef DVB_HANDLE_DVB_DEBUG_H
#define DVB_HANDLE_DVB_DEBUG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int dvb_debug_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("DVB_DEBUG");
        cached = (e && e[0] && strcmp(e, "0") != 0) ? 1 : 0;
    }
    return cached;
}

#define DVBDBG(fmt, ...)                                                       \
    do {                                                                       \
        if (dvb_debug_enabled()) {                                             \
            fprintf(stderr, "[dvb_dbg] " fmt "\n", ##__VA_ARGS__);             \
        }                                                                      \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* DVB_HANDLE_DVB_DEBUG_H */
