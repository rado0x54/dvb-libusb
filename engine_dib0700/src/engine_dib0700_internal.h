/* SPDX-License-Identifier: MIT */
/*
 * Internal types shared between engine_dib0700.c and boards.c.
 */

#ifndef ENGINE_DIB0700_INTERNAL_H
#define ENGINE_DIB0700_INTERNAL_H

#include "dib0700/dib0700.h"
#include <linux/i2c.h>
#include <media/dvb_frontend.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Board-specific bridge bring-up after firmware upload. Called
 * once per device, before any chip attach. Mirrors upstream's
 * `xbox_one_attach`-style early-init blocks (GPIO power + reset).
 * Returns 0 on success, negative errno on failure. */
typedef int (*dib0700_board_bringup_fn)(dib0700_dev_t *bridge);

/* Per-frontend chip attach. Same shape as the em28xx version:
 * given the parent i²c adapter and a frontend index, attach
 * demod + tuner via i2c_new_client_device and return what the
 * engine needs to drive the frontend. */
typedef int (*dib0700_board_attach_fn)(struct i2c_adapter *parent_adap,
                                       int frontend_index,
                                       struct i2c_client **demod_client_out,
                                       struct dvb_frontend **fe_out,
                                       struct i2c_client **tuner_client_out);

typedef struct dib0700_board {
    const char               *name;
    const char              **vidpids;          /* NULL-terminated */
    int                       num_frontends;
    const char               *bridge_firmware;  /* e.g. "dvb-usb-dib0700-1.20.fw" */
    dib0700_board_bringup_fn  bringup;
    dib0700_board_attach_fn   attach;
    const uint8_t            *ts_endpoints;     /* per-frontend bulk-IN EP */
    const uint32_t           *ts_buf_sizes;     /* per-frontend or NULL for default */
    const uint32_t           *ts_pool_depths;   /* per-frontend or NULL for default */
    const uint32_t           *supported_delsys;
    size_t                    supported_delsys_count;
} dib0700_board_t;

/* Sentinel: row with name = NULL. */
extern const dib0700_board_t dib0700_board_table[];

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_DIB0700_INTERNAL_H */
