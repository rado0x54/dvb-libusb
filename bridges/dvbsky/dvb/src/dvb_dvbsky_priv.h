/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Internal types shared between dvb_dvbsky.c and boards.c.
 */

#ifndef ENGINE_DVBSKY_INTERNAL_H
#define ENGINE_DVBSKY_INTERNAL_H

#include "dvbsky/dvbsky.h"
#include <linux/i2c.h>
#include <media/dvb_frontend.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Board-specific bridge bring-up, called once per device before
 * any chip attach. Mirrors upstream `dvbsky_identify_state` for
 * the relevant board (the Mygica T230 family runs the gpio
 * power/reset sequence at 0x04/0x83/0xc0). Returns 0 on success,
 * negative errno on failure. */
typedef int (*dvbsky_board_bringup_fn)(dvbsky_dev_t *bridge);

/* Per-frontend chip attach. Same shape as dib0700 / em28xx:
 * given the parent i²c adapter, attach demod + tuner via
 * i2c_new_client_device and return what the engine needs to drive
 * the frontend. */
typedef int (*dvbsky_board_attach_fn)(struct i2c_adapter *parent_adap,
                                      int frontend_index,
                                      struct i2c_client **demod_client_out,
                                      struct dvb_frontend **fe_out,
                                      struct i2c_client **tuner_client_out);

typedef struct dvbsky_board {
    const char               *name;
    const char              **vidpids;          /* NULL-terminated */
    int                       num_frontends;
    dvbsky_board_bringup_fn   bringup;
    dvbsky_board_attach_fn    attach;
    const uint8_t            *ts_endpoints;     /* per-frontend bulk-IN EP */
    const uint32_t           *ts_buf_sizes;     /* per-frontend or NULL for default */
    const uint32_t           *ts_pool_depths;   /* per-frontend or NULL for default */
    const uint32_t           *supported_delsys;
    size_t                    supported_delsys_count;
} dvbsky_board_t;

/* Sentinel: row with name = NULL. */
extern const dvbsky_board_t dvbsky_board_table[];

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_DVBSKY_INTERNAL_H */
