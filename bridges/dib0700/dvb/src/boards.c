/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dib0700 board table + per-board chip attach + per-board bridge
 * bring-up. Mirror of upstream's dib0700_devices.c device-properties
 * + early-attach blocks.
 */

#include "dvb_dib0700_priv.h"

#include "mn88472/mn88472.h"
#include "tda18250/tda18250.h"
#include <linuxdvbkpi/linuxdvbkpi.h>
#include <linux/dvb/frontend.h>
#include <linux/i2c.h>
#include <media/dvb_frontend.h>

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

/* ---- Microsoft Xbox One Digital TV Tuner ------------------------- *
 *
 * USB IDs: 045e:02d5.
 *
 * Bridge: DiB0700 + bulk-IN EP 0x82.
 * Demod:  Panasonic MN88472 (i²c 0x18, xtal 20.5 MHz, parallel TS).
 * Tuner:  NXP TDA18250 (i²c 0x60, xtal 27 MHz).
 *
 * Bring-up sequence (xbox_one_attach in dib0700_devices.c):
 *   GPIO6 power: 0 → 1 with 30 ms each (frontend power enable).
 *   GPIO10 reset: 1 → 0 → 1 with 30 ms each (demod reset). */

static const char *xbox_one_vidpids[] = {
    "045e:02d5",
    NULL,
};

static int xbox_one_bringup(dib0700_dev_t *bridge) {
    /* fe power enable */
    int rc = dib0700_set_gpio(bridge, DIB0700_GPIO6, DIB0700_GPIO_OUT, 0);
    if (rc < 0) return rc;
    msleep_local(30);
    rc = dib0700_set_gpio(bridge, DIB0700_GPIO6, DIB0700_GPIO_OUT, 1);
    if (rc < 0) return rc;
    msleep_local(30);

    /* demod reset (1 → 0 → 1 with 30 ms each) */
    rc = dib0700_set_gpio(bridge, DIB0700_GPIO10, DIB0700_GPIO_OUT, 1);
    if (rc < 0) return rc;
    msleep_local(30);
    rc = dib0700_set_gpio(bridge, DIB0700_GPIO10, DIB0700_GPIO_OUT, 0);
    if (rc < 0) return rc;
    msleep_local(30);
    rc = dib0700_set_gpio(bridge, DIB0700_GPIO10, DIB0700_GPIO_OUT, 1);
    if (rc < 0) return rc;
    msleep_local(30);
    return 0;
}

static int xbox_one_attach(struct i2c_adapter *parent_adap,
                           int frontend_index,
                           struct i2c_client **demod_client_out,
                           struct dvb_frontend **fe_out,
                           struct i2c_client **tuner_client_out) {
    if (frontend_index != 0) return -EINVAL;

    /* mn88472::probe writes &dev->fe via *config.fe — we hand a
     * pointer-to-pointer it'll fill. */
    struct dvb_frontend *fe_back = NULL;
    struct mn88472_config mn_cfg = {0};
    mn_cfg.xtal       = 20500000;
    mn_cfg.ts_mode    = MN88472_TS_MODE_PARALLEL;
    mn_cfg.ts_clock   = MN88472_TS_CLK_FIXED;
    mn_cfg.i2c_wr_max = 22;
    mn_cfg.fe         = &fe_back;

    struct i2c_board_info mn_info = {0};
    strncpy(mn_info.type, "mn88472", I2C_NAME_SIZE - 1);
    mn_info.addr          = 0x18;
    mn_info.platform_data = &mn_cfg;

    struct i2c_client *demod = i2c_new_client_device(parent_adap, &mn_info);
    if (IS_ERR(demod) || !i2c_client_has_driver(demod)) return -EIO;
    if (!fe_back) {
        i2c_unregister_device(demod);
        return -EIO;
    }

    /* tda18250::probe memcpys tda18250_ops into fe->ops.tuner_ops, so
     * subsequent fe->ops.set_frontend dispatch resolves the tuner
     * automatically. */
    struct tda18250_config tda_cfg = {0};
    tda_cfg.if_dvbt_6   = 3950;
    tda_cfg.if_dvbt_7   = 4450;
    tda_cfg.if_dvbt_8   = 4950;
    tda_cfg.if_dvbc_6   = 4950;
    tda_cfg.if_dvbc_8   = 4950;
    tda_cfg.if_atsc     = 4079;
    tda_cfg.loopthrough = true;
    tda_cfg.xtal_freq   = TDA18250_XTAL_FREQ_27MHZ;
    tda_cfg.fe          = fe_back;

    struct i2c_board_info tda_info = {0};
    strncpy(tda_info.type, "tda18250", I2C_NAME_SIZE - 1);
    tda_info.addr          = 0x60;
    tda_info.platform_data = &tda_cfg;

    struct i2c_client *tuner = i2c_new_client_device(parent_adap, &tda_info);
    if (IS_ERR(tuner) || !i2c_client_has_driver(tuner)) {
        i2c_unregister_device(demod);
        return -EIO;
    }

    *demod_client_out = demod;
    *fe_out           = fe_back;
    *tuner_client_out = tuner;
    return 0;
}

static const uint8_t  xbox_one_ts_endpoints[]  = { DIB0700_TS_BULK_EP_XBOX };
static const uint32_t xbox_one_ts_buf_sizes[]  = { 39480u };  /* DIB0700_DEFAULT_STREAMING_CONFIG */
static const uint32_t xbox_one_ts_pool_depths[] = { 8u };     /* doubled vs upstream's 4 for macOS libusb */

static const uint32_t xbox_one_delsys[] = {
    SYS_DVBT, SYS_DVBT2, SYS_DVBC_ANNEX_A,
};

const dib0700_board_t dib0700_board_table[] = {
    {
        .name             = "Microsoft Xbox One Digital TV Tuner",
        .vidpids          = xbox_one_vidpids,
        .num_frontends    = 1,
        .bridge_firmware  = "dvb-usb-dib0700-1.20.fw",
        .bringup          = xbox_one_bringup,
        .attach           = xbox_one_attach,
        .ts_endpoints     = xbox_one_ts_endpoints,
        .ts_buf_sizes     = xbox_one_ts_buf_sizes,
        .ts_pool_depths   = xbox_one_ts_pool_depths,
        .supported_delsys = xbox_one_delsys,
        .supported_delsys_count =
            sizeof(xbox_one_delsys) / sizeof(xbox_one_delsys[0]),
    },
    {0}, /* sentinel */
};
