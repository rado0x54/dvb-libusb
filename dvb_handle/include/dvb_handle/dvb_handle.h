/* SPDX-License-Identifier: MIT */
/*
 * Shared types for the DVB engine layer + library consumers.
 *
 * Each engine (engine_em28xx, engine_dib0700, …) discovers its
 * supported USB boards, brings them up, and produces one
 * `dvb_frontend_handle_t` per attached frontend. Tools, third-party
 * applications, and any SAT>IP / DVB-server glue all consume
 * handles through the same vtable — interchangeable from the
 * caller's perspective.
 *
 * License: MIT (clean-room types; no upstream-Linux derivation).
 * Note that the wider stack contains GPL-2.0+ code (the lifted chip
 * drivers and the bridge ports) — see README "License" for the
 * combined-work story.
 */

#ifndef DVB_HANDLE_DVB_HANDLE_H
#define DVB_HANDLE_DVB_HANDLE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tuning parameters in upstream Linux DVB API units. Engine vtable
 * `tune` translates these to whatever each engine's chip drivers
 * expect; values match `enum fe_delivery_system` in the kernel
 * uAPI (SYS_DVBT=3, SYS_DVBT2=16, SYS_DVBC_ANNEX_A=1, SYS_ATSC=11,
 * SYS_DVBC_ANNEX_B=2, …). */
typedef struct dvb_tune_params {
    uint32_t delsys;
    uint32_t freq_hz;
    uint32_t bandwidth_hz;     /* DVB-T/T2 channel BW; ignored DVB-C */
    uint32_t symbol_rate;      /* DVB-C only */
    int32_t  stream_id;        /* DVB-T2 PLP; -1 = no filter */
} dvb_tune_params_t;

typedef struct dvb_status {
    bool     has_signal;
    bool     has_carrier;
    bool     has_viterbi;
    bool     has_sync;
    bool     has_lock;
    int32_t  cnr_db_x1000;     /* CNR in dB × 1000, only valid w/ lock */
} dvb_status_t;

/* Engine-erased per-frontend operations. Each engine fills in a
 * vtable that adapts its chip attach + USB stream lifecycle into
 * these signatures. */
typedef struct dvb_engine_vtable {
    int  (*tune)(void *engine_state, const dvb_tune_params_t *p);
    int  (*read_ts)(void *engine_state, void *buf, size_t len,
                    uint32_t timeout_ms);
    int  (*get_status)(void *engine_state, dvb_status_t *out);
    int  (*event_fd)(void *engine_state);
    int  (*capture_stop)(void *engine_state);
    void (*close)(void *engine_state);     /* engine teardown */
} dvb_engine_vtable_t;

/* Per-board record describing a USB device that's currently
 * plugged in and matches one of the engine's supported boards.
 * Returned by `*_scan_present()` — pure libusb enumeration; no
 * device claim, no bridge bring-up, no firmware required.
 *
 * Two physical instances of the same board show up as separate
 * entries with the same `name` / `vidpid` but different
 * `bus_number` / `device_address`. */
typedef struct dvb_present_board {
    const char *bridge;        /* "em28xx", "dib0700" */
    const char *name;          /* board name, e.g. "Hauppauge WinTV-dualHD" */
    const char *vidpid;        /* matching USB ID string, e.g. "2040:0265" */
    int         num_frontends; /* board's frontend count */
    uint8_t     bus_number;
    uint8_t     device_address;
} dvb_present_board_t;

/* Read-only descriptor of a board the engine knows how to drive
 * (whether or not it's currently plugged in). Each engine exposes
 * its compiled-in board table via a `*_supported_boards()`
 * function returning a sentinel-terminated array of these. */
typedef struct dvb_supported_board {
    const char         *bridge;        /* "em28xx", "dib0700" */
    const char         *name;          /* e.g. "Hauppauge WinTV-dualHD" */
    const char *const  *vidpids;       /* NULL-terminated VID:PID list */
    int                 num_frontends;
} dvb_supported_board_t;

/* Per-frontend handle. Owned by the engine that allocated it; the
 * plugin/test does NOT free this — call the engine's shutdown
 * function for that. */
typedef struct dvb_frontend_handle {
    const dvb_engine_vtable_t *ops;
    void                      *engine_state;  /* opaque engine private */

    /* Lock serializing i²c access on the underlying bridge. Two
     * frontend handles from the same physical device share one
     * mutex (em28xx WinTV-dualHD has 2 frontends, one bridge); a
     * single-frontend device gets a private one. */
    pthread_mutex_t           *bridge_lock;

    /* Human-readable name for `ad->name(...)`. Owned by the engine
     * — must outlive every plugin/test reference. */
    const char                *display_name;

    /* Supported delivery systems for `ad->delsys(...)`. Pointer
     * owned by the engine. */
    const uint32_t            *supported_delsys;
    size_t                     supported_delsys_count;
} dvb_frontend_handle_t;

#ifdef __cplusplus
}
#endif

#endif /* DVB_HANDLE_DVB_HANDLE_H */
