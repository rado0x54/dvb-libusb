/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvbsky board table + per-board chip attach + bridge bring-up.
 * Mirror of the T230-family entries in upstream's
 * drivers/media/usb/dvb-usb-v2/dvbsky.c.
 *
 * Boards on the table today (all share the same Mygica T230 power
 * sequence on the bridge — upstream's `dvbsky_identify_state`
 * else-branch — and the same si2168 + si2157/si2141 chip lineup):
 *
 *   - MyGica T230                       (0572:c688)  si2168 + si2157 (if_port=1)
 *   - MyGica T230C / Geniatech EyeTV    (0572:c689)  si2168 + si2141 (if_port=0)
 *   - MyGica T230C2                     (0572:c68a)  si2168 + si2141 (if_port=0,
 *                                                                     TS_CLK_MANUAL)
 *
 * The Geniatech EyeTV Stick reports the T230C product id (per
 * dmesg) and is electrically identical — one row covers both.
 *
 * Adding another T230-family board (T230A, *_LITE) =
 *   1. Add a NULL-terminated VID:PID array.
 *   2. Add an attach function (if the chip config differs).
 *   3. Add a row to dvbsky_board_table[].
 *
 * The T230A variant in upstream uses a DIFFERENT power sequence
 * (gpio_ctrl(0x87)/0x86/0x80 instead of 0x04/0x83/0xc0). Don't
 * reuse t230c_family_bringup for it.
 */

#include "dvb_dvbsky_priv.h"

/* Polyfill umbrella first — chip headers test IS_REACHABLE() and
 * need <linux/kconfig.h> visible. */
#include <linuxdvbkpi/linuxdvbkpi.h>
#include <linux/dvb/frontend.h>
#include <linux/i2c.h>
#include <media/dvb_frontend.h>

#include "si2168/si2168.h"
#include "si2157/si2157.h"

#include <errno.h>
#include <string.h>
#include <time.h>

static void msleep_local(unsigned ms) {
    if (!ms) return;
    struct timespec ts = {
        .tv_sec  = (time_t)(ms / 1000u),
        .tv_nsec = (long)((ms % 1000u) * 1000000UL),
    };
    nanosleep(&ts, NULL);
}

/* ---- Bring-up: shared by the Mygica T230 family ------------------ *
 *
 * Upstream `dvbsky_identify_state` else-branch (non-T230A path):
 *
 *   gpio_ctrl(0x04, 1);                 msleep(20);
 *   gpio_ctrl(0x83, 0); gpio_ctrl(0xc0, 1);  msleep(100);
 *   gpio_ctrl(0x83, 1); gpio_ctrl(0xc0, 0);  msleep(50);
 *
 * The first call enables a high-side rail; the 0x83/0xc0 pair toggles
 * the demod's nRST + power rail to assert reset, then deasserts. */
static int t230_family_bringup(dvbsky_dev_t *bridge) {
    int rc;
    rc = dvbsky_gpio_ctrl(bridge, 0x04, 1); if (rc < 0) return rc;
    msleep_local(20);
    rc = dvbsky_gpio_ctrl(bridge, 0x83, 0); if (rc < 0) return rc;
    rc = dvbsky_gpio_ctrl(bridge, 0xc0, 1); if (rc < 0) return rc;
    msleep_local(100);
    rc = dvbsky_gpio_ctrl(bridge, 0x83, 1); if (rc < 0) return rc;
    rc = dvbsky_gpio_ctrl(bridge, 0xc0, 0); if (rc < 0) return rc;
    msleep_local(50);
    return 0;
}

/* ---- Shared si2168 attach helper. The three boards differ only in
 * ts_mode flags (TS_CLK_MANUAL) and the tuner attach (if_port +
 * si2141 vs si2157 chip-name hint). */

static int attach_si2168(struct i2c_adapter *parent_adap,
                         uint8_t ts_clk_manual,
                         struct dvb_frontend **fe_out,
                         struct i2c_adapter **muxed_adap_out,
                         struct i2c_client  **demod_client_out) {
    struct dvb_frontend *fe_back    = NULL;
    struct i2c_adapter  *muxed_adap = NULL;
    struct si2168_config si_cfg = {
        .fe                  = &fe_back,
        .i2c_adapter         = &muxed_adap,
        .ts_mode             = (uint8_t)(SI2168_TS_PARALLEL |
                                         (ts_clk_manual ? SI2168_TS_CLK_MANUAL : 0)),
        .ts_clock_inv        = 1,
        .ts_clock_gapped     = 0,
        .spectral_inversion  = 0,
    };
    struct i2c_board_info si_info = {0};
    strncpy(si_info.type, "si2168", I2C_NAME_SIZE - 1);
    si_info.addr          = 0x64;
    si_info.platform_data = &si_cfg;

    struct i2c_client *demod = i2c_new_client_device(parent_adap, &si_info);
    if (IS_ERR(demod) || !i2c_client_has_driver(demod)) return -EIO;
    if (!fe_back || !muxed_adap) {
        i2c_unregister_device(demod);
        return -EIO;
    }
    *fe_out           = fe_back;
    *muxed_adap_out   = muxed_adap;
    *demod_client_out = demod;
    return 0;
}

static int attach_tuner_si2157_like(struct i2c_adapter *muxed_adap,
                                    struct dvb_frontend *fe,
                                    const char *chip_type,
                                    int if_port,
                                    struct i2c_client **tuner_client_out) {
    struct si2157_config sit_cfg = {
        .fe                  = fe,
        .inversion           = 0,
        .dont_load_firmware  = 0,
        .if_port             = (uint8_t)if_port,
    };
    struct i2c_board_info sit_info = {0};
    strncpy(sit_info.type, chip_type, I2C_NAME_SIZE - 1);
    sit_info.addr          = 0x60;
    sit_info.platform_data = &sit_cfg;

    struct i2c_client *tuner = i2c_new_client_device(muxed_adap, &sit_info);
    if (IS_ERR(tuner) || !i2c_client_has_driver(tuner)) return -EIO;

    *tuner_client_out = tuner;
    return 0;
}

/* ---- T230 (0572:c688) ------------------------------------------- *
 *
 * si2168 @ 0x64, plain TS_PARALLEL.
 * si2157 @ 0x60 on the muxed bus, if_port = 1. */
static int t230_attach(struct i2c_adapter *parent_adap,
                       int frontend_index,
                       struct i2c_client **demod_client_out,
                       struct dvb_frontend **fe_out,
                       struct i2c_client **tuner_client_out) {
    if (frontend_index != 0) return -EINVAL;
    struct dvb_frontend *fe_back    = NULL;
    struct i2c_adapter  *muxed_adap = NULL;
    int rc = attach_si2168(parent_adap, /*ts_clk_manual=*/0,
                           &fe_back, &muxed_adap, demod_client_out);
    if (rc < 0) return rc;
    rc = attach_tuner_si2157_like(muxed_adap, fe_back, "si2157", 1,
                                  tuner_client_out);
    if (rc < 0) {
        i2c_unregister_device(*demod_client_out);
        *demod_client_out = NULL;
        return rc;
    }
    *fe_out = fe_back;
    return 0;
}

/* ---- T230C / Geniatech EyeTV Stick (0572:c689) ------------------ *
 *
 * si2168 @ 0x64, TS_PARALLEL.
 * si2141 @ 0x60 on the muxed bus, if_port = 0. The "si2141" name
 * routes through the si2157 driver's id_table (it shares the chip
 * family) but matches the SI2141 part_id slot. */
static int t230c_attach(struct i2c_adapter *parent_adap,
                        int frontend_index,
                        struct i2c_client **demod_client_out,
                        struct dvb_frontend **fe_out,
                        struct i2c_client **tuner_client_out) {
    if (frontend_index != 0) return -EINVAL;
    struct dvb_frontend *fe_back    = NULL;
    struct i2c_adapter  *muxed_adap = NULL;
    int rc = attach_si2168(parent_adap, /*ts_clk_manual=*/0,
                           &fe_back, &muxed_adap, demod_client_out);
    if (rc < 0) return rc;
    rc = attach_tuner_si2157_like(muxed_adap, fe_back, "si2141", 0,
                                  tuner_client_out);
    if (rc < 0) {
        i2c_unregister_device(*demod_client_out);
        *demod_client_out = NULL;
        return rc;
    }
    *fe_out = fe_back;
    return 0;
}

/* ---- T230C2 (0572:c68a) ----------------------------------------- *
 *
 * Same as T230C, but the demod uses TS_PARALLEL | TS_CLK_MANUAL
 * (per upstream's idProduct branch in dvbsky_mygica_t230c_attach). */
static int t230c2_attach(struct i2c_adapter *parent_adap,
                         int frontend_index,
                         struct i2c_client **demod_client_out,
                         struct dvb_frontend **fe_out,
                         struct i2c_client **tuner_client_out) {
    if (frontend_index != 0) return -EINVAL;
    struct dvb_frontend *fe_back    = NULL;
    struct i2c_adapter  *muxed_adap = NULL;
    int rc = attach_si2168(parent_adap, /*ts_clk_manual=*/1,
                           &fe_back, &muxed_adap, demod_client_out);
    if (rc < 0) return rc;
    rc = attach_tuner_si2157_like(muxed_adap, fe_back, "si2141", 0,
                                  tuner_client_out);
    if (rc < 0) {
        i2c_unregister_device(*demod_client_out);
        *demod_client_out = NULL;
        return rc;
    }
    *fe_out = fe_back;
    return 0;
}

/* ---- VID:PID lists ---------------------------------------------- */

static const char *t230_vidpids[]   = { "0572:c688", NULL };
static const char *t230c_vidpids[]  = { "0572:c689", NULL };
static const char *t230c2_vidpids[] = { "0572:c68a", NULL };

/* All three boards stream from EP 0x82. The defaults in the
 * dvb_dvbsky engine pick up DVBSKY_TS_BULK_EP if .ts_endpoints is
 * NULL, but we set it explicitly here to keep the table greppable. */
static const uint8_t  t230_family_ts_endpoints[] = { DVBSKY_TS_BULK_EP };

static const uint32_t t230_family_delsys[] = {
    SYS_DVBT, SYS_DVBT2, SYS_DVBC_ANNEX_A,
};

const dvbsky_board_t dvbsky_board_table[] = {
    {
        .name             = "MyGica T230",
        .vidpids          = t230_vidpids,
        .num_frontends    = 1,
        .bringup          = t230_family_bringup,
        .attach           = t230_attach,
        .ts_endpoints     = t230_family_ts_endpoints,
        .supported_delsys = t230_family_delsys,
        .supported_delsys_count =
            sizeof(t230_family_delsys) / sizeof(t230_family_delsys[0]),
    },
    {
        .name             = "MyGica T230C / Geniatech EyeTV Stick",
        .vidpids          = t230c_vidpids,
        .num_frontends    = 1,
        .bringup          = t230_family_bringup,
        .attach           = t230c_attach,
        .ts_endpoints     = t230_family_ts_endpoints,
        .supported_delsys = t230_family_delsys,
        .supported_delsys_count =
            sizeof(t230_family_delsys) / sizeof(t230_family_delsys[0]),
    },
    {
        .name             = "MyGica T230C2",
        .vidpids          = t230c2_vidpids,
        .num_frontends    = 1,
        .bringup          = t230_family_bringup,
        .attach           = t230c2_attach,
        .ts_endpoints     = t230_family_ts_endpoints,
        .supported_delsys = t230_family_delsys,
        .supported_delsys_count =
            sizeof(t230_family_delsys) / sizeof(t230_family_delsys[0]),
    },
    {0}, /* sentinel */
};
