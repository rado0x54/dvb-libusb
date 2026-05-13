/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvb_dvbsky — bridge-generic engine for DVB USB devices built on
 * the "dvbsky" USB bridge family (DVBSky, MyGica T230 line,
 * Geniatech EyeTV Stick, and TechnoTrend/TerraTec rebrands of the
 * same hardware). Same shape as dvb_em28xx / dvb_dib0700 — a board
 * table inside, scans USB for matches, runs board-specific GPIO
 * bring-up + chip attach.
 *
 * Today: T230 / T230C / T230C2 (Mygica + Geniatech EyeTV Stick).
 * DVB-S2 boards (S960 etc.) and CI-equipped boards (S960C, T680C)
 * are not yet on the table — they need additional chip drivers
 * (m88ds3103, ts2020, sp2) that aren't in chips/lifted/.
 */

#ifndef ENGINE_DVBSKY_ENGINE_DVBSKY_H
#define ENGINE_DVBSKY_ENGINE_DVBSKY_H

#include "dvb_handle/dvb_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

int  dvb_dvbsky_discover_all(dvb_frontend_handle_t **handles, int max);
int  dvb_dvbsky_open(const char *vidpid,
                     dvb_frontend_handle_t **handles, int max);
void dvb_dvbsky_shutdown(void);

/* Read-only enumeration of every board this engine knows. See
 * dvb_em28xx_supported_boards() for semantics. */
const dvb_supported_board_t *dvb_dvbsky_supported_boards(int *count_out);

/* Scan plugged-in USB devices against the board table. See
 * dvb_em28xx_scan_present() for semantics. No firmware required. */
int dvb_dvbsky_scan_present(dvb_present_board_t *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_DVBSKY_ENGINE_DVBSKY_H */
