/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Internal types shared between dvb_em28xx.c (bridge lifecycle)
 * and boards.c (per-board chip attach). Not part of the public API.
 */

#ifndef ENGINE_EM28XX_INTERNAL_H
#define ENGINE_EM28XX_INTERNAL_H

#include "em28xx/em28xx.h"
#include <linux/i2c.h>
#include <media/dvb_frontend.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-frontend chip attach function. Given the parent i²c adapter
 * (em28xx bus 1, in practice) and the frontend index, attaches the
 * board's demod + tuner via i2c_new_client_device, captures the
 * dvb_frontend pointer the demod's probe wrote back, and returns
 * everything the engine needs to drive that frontend.
 *
 * Returns 0 on success, negative errno on failure. On failure the
 * function MUST NOT leave dangling i²c clients — the engine treats
 * non-zero as "tear down nothing further".
 *
 * Chip configs (struct si2168_config, struct lgdt3306a_config, …)
 * live inside the attach function, so this header doesn't need to
 * include every chip header. */
typedef int (*em28xx_board_attach_fn)(struct i2c_adapter *parent_adap,
                                      int frontend_index,
                                      struct i2c_client **demod_client_out,
                                      struct dvb_frontend **fe_out,
                                      struct i2c_client **tuner_client_out);

/* Per-board record. Static const data describing one supported
 * em28xx-based DVB USB device. The attach function is the only
 * piece that touches chip-specific configs. */
typedef struct em28xx_board {
    const char              *name;             /* "Hauppauge WinTV-dualHD" */
    const char             **vidpids;          /* NULL-terminated list */
    int                      num_frontends;    /* 1 or 2 today */
    uint8_t                  i2c_speed;        /* EM28XX_I2C_* bits */
    uint8_t                  xclk;             /* EM28XX_XCLK_* bits */
    const em28xx_reg_seq_t  *gpio_seq;         /* DVB-mode GPIO sequence */
    const uint8_t           *ts_endpoints;     /* per-frontend EP, length = num_frontends */
    em28xx_board_attach_fn   attach;
    const uint32_t          *supported_delsys; /* enum fe_delivery_system */
    size_t                   supported_delsys_count;
} em28xx_board_t;

/* The board table — terminated by a sentinel row whose `name` is
 * NULL. Defined in boards.c. */
extern const em28xx_board_t em28xx_board_table[];

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_EM28XX_INTERNAL_H */
