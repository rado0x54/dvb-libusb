/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvb_dib0700 — bridge-generic engine for all DVB USB devices
 * built on the DiBcom DiB0700 USB bridge family. Same shape as
 * dvb_em28xx — a board table inside, scans USB for matches,
 * runs board-specific bring-up + chip attach.
 *
 * Today: 1 board (Microsoft Xbox One Digital TV Tuner). Future
 * dib0700 boards add rows to src/boards.c, not new files.
 */

#ifndef ENGINE_DIB0700_ENGINE_DIB0700_H
#define ENGINE_DIB0700_ENGINE_DIB0700_H

#include "dvb_handle/dvb_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

int  dvb_dib0700_discover_all(dvb_frontend_handle_t **handles, int max);
int  dvb_dib0700_open(const char *vidpid,
                         dvb_frontend_handle_t **handles, int max);
void dvb_dib0700_shutdown(void);

/* Read-only enumeration of every board this engine knows. See
 * dvb_em28xx_supported_boards() for semantics. */
const dvb_supported_board_t *dvb_dib0700_supported_boards(int *count_out);

/* Scan plugged-in USB devices against the board table. See
 * dvb_em28xx_scan_present() for semantics. No firmware required. */
int dvb_dib0700_scan_present(dvb_present_board_t *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_DIB0700_ENGINE_DIB0700_H */
