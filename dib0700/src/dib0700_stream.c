/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dib0700 streaming control — flips the bulk-IN endpoint on/off.
 *
 * REQUEST_ENABLE_VIDEO (0x0F) takes a 4-byte payload:
 *
 *   buf[0] = 0x0F                                 — request opcode
 *   buf[1] = (onoff << 4) | 0x00                  — 0x10 = enable, 0x00 = disable
 *                                                   low nibble = video mode (0 = MPEG-TS)
 *   buf[2] = master_mode<<4 | channel_state       — high nibble: 1 = master mode
 *                                                   low nibble: bit per active adapter
 *   buf[3] = 0x00                                 — reserved
 *
 * The xbox board sets `disable_streaming_master_mode = 1`, in which
 * case the master-mode nibble stays zero. channel_state accumulates
 * across multi-adapter boards (bit 0 = adapter 0, bit 1 = adapter 1).
 *
 * Firmware ≥ 1.20.1 also wants REQUEST_SET_USB_XFER_LEN issued before
 * enable to set the per-URB TS-packet count. We default to 21 packets
 * × 188 = 3948 bytes per URB, matching upstream's
 * `nb_packet_buffer_size` default.
 */

#include "dib0700_priv.h"

#include <errno.h>
#include <pthread.h>

#define DIB0700_DEFAULT_TS_PACKETS_PER_URB  21u

static int set_usb_xfer_len(struct dib0700_dev *dev, uint16_t nb_ts_packets) {
    /* Only valid on firmware ≥ 1.20.1. Upstream silently drops the
     * call on older firmware. */
    if (dev->fw_version < 0x10201) return 0;
    pthread_mutex_lock(&dev->ctrl_lock);
    dev->buf[0] = DIB0700_REQ_SET_USB_XFER_LEN;
    dev->buf[1] = (uint8_t)((nb_ts_packets >> 8) & 0xff);
    dev->buf[2] = (uint8_t)(nb_ts_packets & 0xff);
    int rc = dib0700_ctrl_wr(dev, dev->buf, 3);
    pthread_mutex_unlock(&dev->ctrl_lock);
    return rc;
}

int dib0700_streaming_ctrl(dib0700_dev_t *dev, int adapter_idx,
                           int onoff, int disable_master) {
    if (!dev || adapter_idx < 0 || adapter_idx > 7) return -EINVAL;

    if (onoff) {
        int rc = set_usb_xfer_len(dev, DIB0700_DEFAULT_TS_PACKETS_PER_URB);
        if (rc < 0) return rc;
    }

    pthread_mutex_lock(&dev->ctrl_lock);
    if (onoff) {
        dev->channel_state |= (uint8_t)(1u << adapter_idx);
    } else {
        dev->channel_state &= (uint8_t)~(1u << adapter_idx);
    }

    dev->buf[0] = DIB0700_REQ_ENABLE_VIDEO;
    dev->buf[1] = (uint8_t)((onoff ? 1 : 0) << 4);
    dev->buf[2] = disable_master ? dev->channel_state
                                 : (uint8_t)((1u << 4) | dev->channel_state);
    dev->buf[3] = 0x00;
    int rc = dib0700_ctrl_wr(dev, dev->buf, 4);
    pthread_mutex_unlock(&dev->ctrl_lock);
    return rc;
}
