/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * engine_em28xx — bridge-generic engine for all DVB USB devices
 * built on the Empia em28xx USB→I²C/TS bridge family.
 *
 * Mirrors upstream's `dvb_usb_em28xx` driver: one driver, big board
 * table inside, scans USB for any matching VID:PID and brings up
 * whichever board the table describes. Per-board variation
 * (chip choices, i²c addresses, GPIO sequence, frontend count)
 * lives in src/boards.cpp; the bridge lifecycle (open/firmware/
 * tune/streaming) is in src/engine_em28xx.cpp.
 *
 * The engine is consumed two ways:
 *
 *   - Library consumers (a SAT>IP / DVB-server plugin, or any user
 *     application) call `engine_em28xx_discover_all()` at startup to
 *     enumerate every plugged-in supported em28xx-based DVB device.
 *
 *   - The bundled diagnostic tools (tools/dvb_pid_dump,
 *     tools/dvb_devices) call the same API with no SAT>IP /
 *     streaming-server dependency.
 *
 * Either way, callers receive `dvb_frontend_handle_t *` instances
 * with the same shape; the same vtable drives tune/read/status.
 */

#ifndef ENGINE_EM28XX_ENGINE_EM28XX_H
#define ENGINE_EM28XX_ENGINE_EM28XX_H

#include "dvb_handle/dvb_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Discover every plugged-in supported em28xx-based DVB device.
 * Walks the internal board table, opens each matching device,
 * brings up its bridge with the board's GPIO sequence, attaches
 * frontends, and appends per-frontend handles to *handles up to
 * max. Returns the number appended. */
int engine_em28xx_discover_all(dvb_frontend_handle_t **handles, int max);

/* Open exactly the device at the given USB vid:pid string
 * ("2040:0265"). Convenience for hardware test programs. The board
 * record is selected by VID:PID match; frontends 0..N-1 are
 * appended to *handles up to max. Returns the number appended (0
 * if the device isn't supported, isn't plugged in, or bring-up
 * fails). */
int engine_em28xx_open(const char *vidpid,
                       dvb_frontend_handle_t **handles, int max);

/* Tear down everything the engine opened: chip clients, em28xx
 * bridges, USB devices. Idempotent. Caller must have stopped using
 * any handles before calling. */
void engine_em28xx_shutdown(void);

/* Read-only enumeration of every board the engine's compiled-in
 * table knows how to drive (independent of what's plugged in).
 * Returns a pointer to a static array; *count_out is set to the
 * number of entries. Pointer + entries are valid for the lifetime
 * of the process. No USB / hardware contact. */
const dvb_supported_board_t *engine_em28xx_supported_boards(int *count_out);

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_EM28XX_ENGINE_EM28XX_H */
