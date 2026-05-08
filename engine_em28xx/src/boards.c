/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * em28xx board table + per-board chip attach functions. Mirror of
 * upstream's `em28xx_boards[]` (drivers/media/usb/em28xx/em28xx-cards.c)
 * + the `em28174_dvb_init_*` family (em28xx-dvb.c) — board metadata
 * plus the chip-attach recipe each card needs.
 *
 * Adding a new em28xx-based DVB device =
 *   1. Add a NULL-terminated VID:PID array.
 *   2. Add an attach function below using i2c_new_client_device.
 *   3. Add a row to em28xx_board_table[].
 *
 * No new file, no new build target — same pattern the kernel uses
 * for adding boards to dvb_usb_em28xx.
 */

#include "engine_em28xx_internal.h"

/* Polyfill umbrella first — chip headers (lgdt3306a) test
 * IS_REACHABLE() and need <linux/kconfig.h> visible. */
#include <linuxdvbkpi/linuxdvbkpi.h>
#include <linux/dvb/frontend.h>
#include <linux/i2c.h>
#include <media/dvb_frontend.h>

#include "si2168/si2168.h"
#include "si2157/si2157.h"
#include "lgdt3306a/lgdt3306a.h"

#include <errno.h>
#include <string.h>

/* ---- Hauppauge WinTV-dualHD (DVB-T/T2/C) ------------------------- *
 *
 * USB IDs: 2040:0265.
 *
 * Bridge: em28174 + dual-TS bulk endpoints 0x84 / 0x85.
 * Demod:  Si2168 ×2 (0x64 primary, 0x67 secondary) on i²c bus 1.
 *         Each demod exposes a gated i²c bus to its tuner.
 * Tuner:  Si2157 ×2 (0x60 primary, 0x63 secondary) on the muxed bus.
 *
 * Firmware: si2168 needs dvb-demod-si2168-b40-01.fw (loaded via
 * request_firmware → linuxdvbkpi). Caller sets $FIRMWARE_DIR. */

static const char *wintv_dualhd_dvb_vidpids[] = {
    "2040:0265",
    NULL,
};

static const em28xx_reg_seq_t wintv_dualhd_dvb_gpio[] = {
    { EM2874_R80_GPIO_P0_CTRL, 0xff, 0xff,   0 },
    { 0x0d,                    0xff, 0xff, 200 },
    { 0x50,                    0x04, 0xff, 300 },
    { EM2874_R80_GPIO_P0_CTRL, 0xbf, 0xff, 100 },
    { EM2874_R80_GPIO_P0_CTRL, 0xff, 0xff, 100 },
    { EM2874_R80_GPIO_P0_CTRL, 0xdf, 0xff, 100 },
    { EM2874_R80_GPIO_P0_CTRL, 0xff, 0xff, 100 },
    { EM2874_R5F_TS_ENABLE,    0x00, 0xff,  50 },
    { EM2874_R5D_TS1_PKT_SIZE, 0x05, 0xff,  50 },
    { EM2874_R5E_TS2_PKT_SIZE, 0x05, 0xff,  50 },
    { -1, -1, -1, -1 },
};

static const uint8_t wintv_dualhd_dvb_ts_endpoints[] = {
    EM28XX_TS1_BULK_EP, EM28XX_TS2_BULK_EP,
};

static const uint32_t wintv_dualhd_dvb_delsys[] = {
    SYS_DVBT, SYS_DVBT2, SYS_DVBC_ANNEX_A,
};

static int wintv_dualhd_dvb_attach(struct i2c_adapter *parent_adap,
                                   int frontend_index,
                                   struct i2c_client **demod_client_out,
                                   struct dvb_frontend **fe_out,
                                   struct i2c_client **tuner_client_out) {
    if (frontend_index != 0 && frontend_index != 1) return -EINVAL;
    uint8_t demod_addr = (frontend_index == 0) ? 0x64 : 0x67;
    uint8_t tuner_addr = (frontend_index == 0) ? 0x60 : 0x63;

    /* Attach si2168 demod. Its probe writes *cfg.fe (the demod's
     * dvb_frontend) and *cfg.i2c_adapter (the muxed bus exposed to
     * the tuner — every xfer through it triggers select/deselect). */
    struct dvb_frontend *fe_back     = NULL;
    struct i2c_adapter  *muxed_adap  = NULL;
    struct si2168_config si_cfg = {
        .fe                  = &fe_back,
        .i2c_adapter         = &muxed_adap,
        .ts_mode             = SI2168_TS_SERIAL,
        .ts_clock_inv        = 0,
        .ts_clock_gapped     = 0,
        .spectral_inversion  = 1,
    };
    struct i2c_board_info si_info = {0};
    strncpy(si_info.type, "si2168", I2C_NAME_SIZE - 1);
    si_info.addr          = demod_addr;
    si_info.platform_data = &si_cfg;

    struct i2c_client *demod = i2c_new_client_device(parent_adap, &si_info);
    if (IS_ERR(demod) || !i2c_client_has_driver(demod)) return -EIO;
    if (!fe_back || !muxed_adap) {
        i2c_unregister_device(demod);
        return -EIO;
    }

    /* Attach si2157 tuner on the muxed bus. dont_load_firmware = 1
     * is right for the dualHD's Si2157 (ROM firmware works). */
    struct si2157_config sit_cfg = {
        .fe                  = fe_back,
        .inversion           = 0,
        .dont_load_firmware  = 1,
        .if_port             = 1,
    };
    struct i2c_board_info sit_info = {0};
    strncpy(sit_info.type, "si2157", I2C_NAME_SIZE - 1);
    sit_info.addr          = tuner_addr;
    sit_info.platform_data = &sit_cfg;

    struct i2c_client *tuner = i2c_new_client_device(muxed_adap, &sit_info);
    if (IS_ERR(tuner) || !i2c_client_has_driver(tuner)) {
        i2c_unregister_device(demod);
        return -EIO;
    }

    *demod_client_out = demod;
    *fe_out           = fe_back;
    *tuner_client_out = tuner;
    return 0;
}

/* ---- Hauppauge WinTV-dualHD 01595 ATSC/QAM ---------------------- *
 *
 * USB IDs: 2040:026d, 826d, 826e, 826f, 8270, 8271 (per upstream
 * em28xx-cards.c — multiple SKU variants of the same board).
 *
 * Bridge: em28174 + dual-TS bulk endpoints 0x84 / 0x85.
 *         Same i2c_speed + xclk + GPIO sequence as the DVB dualHD.
 * Demod:  LGDT3306A ×2 (0x59 primary, 0x0e secondary) on i²c bus 1.
 *         Each demod exposes a gated i²c bus to its tuner.
 * Tuner:  Si2157 ×2 (0x60 primary, 0x62 secondary) on the muxed bus,
 *         configured with inversion=1, if_port=1 (different from
 *         the DVB dualHD: that one uses inversion=0 + addr 0x60/0x63).
 *
 * No external firmware blob — LGDT3306A and si2157 both ROM-only on
 * this board.
 *
 * Delivery systems: ATSC (8-VSB OTA) + DVBC_ANNEX_B (QAM cable). */

static const char *wintv_dualhd_atsc_vidpids[] = {
    "2040:026d",
    "2040:826d",
    "2040:826e",
    "2040:826f",
    "2040:8270",
    "2040:8271",
    NULL,
};

static const uint8_t wintv_dualhd_atsc_ts_endpoints[] = {
    EM28XX_TS1_BULK_EP, EM28XX_TS2_BULK_EP,
};

static const uint32_t wintv_dualhd_atsc_delsys[] = {
    SYS_ATSC, SYS_DVBC_ANNEX_B,
};

/* The per-board lgdt3306a recipe — copied verbatim from upstream
 * em28xx-dvb.c `hauppauge_01595_lgdt3306a_config` (the .fe and
 * .i2c_adapter pointer-to-pointer fields are filled in per-call). */
static const struct lgdt3306a_config k_hauppauge_01595_lgdt3306a_template = {
    .qam_if_khz         = 4000,
    .vsb_if_khz         = 3250,
    .deny_i2c_rptr      = 0,
    .spectral_inversion = 1,
    .mpeg_mode          = LGDT3306A_MPEG_SERIAL,
    .tpclk_edge         = LGDT3306A_TPCLK_RISING_EDGE,
    .tpvalid_polarity   = LGDT3306A_TP_VALID_HIGH,
    .xtalMHz            = 25,
};

static int wintv_dualhd_atsc_attach(struct i2c_adapter *parent_adap,
                                    int frontend_index,
                                    struct i2c_client **demod_client_out,
                                    struct dvb_frontend **fe_out,
                                    struct i2c_client **tuner_client_out) {
    if (frontend_index != 0 && frontend_index != 1) return -EINVAL;
    uint8_t demod_addr = (frontend_index == 0) ? 0x59 : 0x0e;
    uint8_t tuner_addr = (frontend_index == 0) ? 0x60 : 0x62;

    struct dvb_frontend *fe_back     = NULL;
    struct i2c_adapter  *muxed_adap  = NULL;
    struct lgdt3306a_config lg_cfg = k_hauppauge_01595_lgdt3306a_template;
    lg_cfg.fe          = &fe_back;
    lg_cfg.i2c_adapter = &muxed_adap;

    struct i2c_board_info lg_info = {0};
    strncpy(lg_info.type, "lgdt3306a", I2C_NAME_SIZE - 1);
    lg_info.addr          = demod_addr;
    lg_info.platform_data = &lg_cfg;

    struct i2c_client *demod = i2c_new_client_device(parent_adap, &lg_info);
    if (IS_ERR(demod) || !i2c_client_has_driver(demod)) return -EIO;
    if (!fe_back || !muxed_adap) {
        i2c_unregister_device(demod);
        return -EIO;
    }

    /* si2157 on the muxed bus. ATSC variant uses inversion=1 (DVB
     * variant uses 0). dont_load_firmware=1 keeps the ROM. */
    struct si2157_config sit_cfg = {
        .fe                  = fe_back,
        .inversion           = 1,
        .dont_load_firmware  = 1,
        .if_port             = 1,
    };
    struct i2c_board_info sit_info = {0};
    strncpy(sit_info.type, "si2157", I2C_NAME_SIZE - 1);
    sit_info.addr          = tuner_addr;
    sit_info.platform_data = &sit_cfg;

    struct i2c_client *tuner = i2c_new_client_device(muxed_adap, &sit_info);
    if (IS_ERR(tuner) || !i2c_client_has_driver(tuner)) {
        i2c_unregister_device(demod);
        return -EIO;
    }

    *demod_client_out = demod;
    *fe_out           = fe_back;
    *tuner_client_out = tuner;
    return 0;
}

/* ---- Board table -------------------------------------------------- *
 *
 * Sentinel row (name = NULL) terminates iteration. To add a board,
 * insert a row above the sentinel. Per-board metadata (USB IDs,
 * bring-up sequence, attach fn) is the only thing that varies. */

const em28xx_board_t em28xx_board_table[] = {
    {
        .name             = "Hauppauge WinTV-dualHD",
        .vidpids          = wintv_dualhd_dvb_vidpids,
        .num_frontends    = 2,
        .i2c_speed        = EM28XX_I2C_CLK_WAIT_ENABLE | EM28XX_I2C_FREQ_400_KHZ,
        .xclk             = EM28XX_XCLK_IR_RC5_MODE | EM28XX_XCLK_FREQUENCY_12MHZ,
        .gpio_seq         = wintv_dualhd_dvb_gpio,
        .ts_endpoints     = wintv_dualhd_dvb_ts_endpoints,
        .attach           = wintv_dualhd_dvb_attach,
        .supported_delsys = wintv_dualhd_dvb_delsys,
        .supported_delsys_count =
            sizeof(wintv_dualhd_dvb_delsys) / sizeof(wintv_dualhd_dvb_delsys[0]),
    },
    {
        /* Same em28xx bring-up as the DVB dualHD; only the demod
         * (LGDT3306A vs. Si2168) and tuner config differ. */
        .name             = "Hauppauge WinTV-dualHD 01595 ATSC/QAM",
        .vidpids          = wintv_dualhd_atsc_vidpids,
        .num_frontends    = 2,
        .i2c_speed        = EM28XX_I2C_CLK_WAIT_ENABLE | EM28XX_I2C_FREQ_400_KHZ,
        .xclk             = EM28XX_XCLK_IR_RC5_MODE | EM28XX_XCLK_FREQUENCY_12MHZ,
        .gpio_seq         = wintv_dualhd_dvb_gpio,
        .ts_endpoints     = wintv_dualhd_atsc_ts_endpoints,
        .attach           = wintv_dualhd_atsc_attach,
        .supported_delsys = wintv_dualhd_atsc_delsys,
        .supported_delsys_count =
            sizeof(wintv_dualhd_atsc_delsys) / sizeof(wintv_dualhd_atsc_delsys[0]),
    },
    {0}, /* sentinel */
};
