/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * em28xx TS-bus capture-enable.
 *
 * Direct port of the bulk-mode path of em28xx_capture_start from
 * drivers/media/usb/em28xx/em28xx-core.c (torvalds/linux @ 9207d47f,
 * lines 682–718). Two register writes per TS index:
 *
 *   1. Set the TS packet-size register (0x5d for TS1, 0x5e for TS2)
 *      to 0xff. In bulk mode this is the max-transfer-size selector
 *      (upstream comment: "Max Tx Size = 188 * 256 = 48128"). In
 *      isoc mode this would be `dvb_max_pkt_size_isoc / 188` — we
 *      don't support isoc here.
 *
 *   2. Update the TS_ENABLE register (0x5f) bits CAPTURE_ENABLE +
 *      FILTER_ENABLE + NULL_DISCARD for the selected TS.
 *
 * Together those tell the bridge to pump TS bytes onto bulk-IN
 * endpoint 0x84 (TS1) / 0x85 (TS2). Without them, the demod's TS
 * output is on the wire to the bridge but the bridge doesn't
 * forward any of it to USB.
 */

#include "em28xx_priv.h"

#include <errno.h>

int em28xx_capture_start(em28xx_dev_t *dev, int ts_index, int enable) {
    if (!dev || (ts_index != 0 && ts_index != 1)) {
        return -EINVAL;
    }

    /* The bridge variants we care about — em28174 / em28178 — all
     * use the em2874-style TS register block (0x5d..0x5f). The
     * older em28xx (em2820 etc.) uses a different register, but
     * those don't have DVB anyway. We assert chip_id is one of the
     * em2874+ family. */
    int chip = em28xx_chip_id(dev);
    if (chip != EM28XX_CHIP_ID_EM2874 &&
        chip != EM28XX_CHIP_ID_EM2884 &&
        chip != EM28XX_CHIP_ID_EM28174 &&
        chip != EM28XX_CHIP_ID_EM28178) {
        return -ENOTSUP;
    }

    int ret;

    /* TS packet-size register: 0xff = max group size for bulk mode.
     * Per upstream "Max Tx Size = 188 * 256 = 48128 = LCM(188,512) * 2"
     * — i.e. the bridge bursts up to ~48 KB before yielding the
     * USB pipe. */
    uint16_t pkt_size_reg = (ts_index == 0)
        ? EM2874_R5D_TS1_PKT_SIZE
        : EM2874_R5E_TS2_PKT_SIZE;
    ret = em28xx_write_reg(dev, pkt_size_reg, 0xff);
    if (ret < 0) {
        return ret;
    }

    /* TS_ENABLE bits per index. NULL_DISCARD makes the bridge drop
     * NULL TS packets (PID 0x1fff) — saves USB bandwidth, no info
     * loss for our use case. FILTER_ENABLE turns on the bridge's
     * TS-PID filter (we set it up downstream as part of plugin
     * de-stub; for raw streaming it just passes everything). */
    uint8_t mask, val;
    if (ts_index == 0) {
        mask = EM2874_TS1_CAPTURE_ENABLE |
               EM2874_TS1_FILTER_ENABLE  |
               EM2874_TS1_NULL_DISCARD;
        val  = enable ? EM2874_TS1_CAPTURE_ENABLE : 0;
    } else {
        mask = EM2874_TS2_CAPTURE_ENABLE |
               EM2874_TS2_FILTER_ENABLE  |
               EM2874_TS2_NULL_DISCARD;
        val  = enable ? EM2874_TS2_CAPTURE_ENABLE : 0;
    }
    return em28xx_write_reg_bits(dev, EM2874_R5F_TS_ENABLE, val, mask);
}
