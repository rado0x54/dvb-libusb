/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DIB0700_PRIV_H
#define DIB0700_PRIV_H

#include "dib0700/dib0700.h"

#include <linux/i2c.h>
#include <pthread.h>
#include <stdint.h>

/* dib0700 USB request opcodes (upstream's REQUEST_*). */
#define DIB0700_REQ_SET_USB_XFER_LEN    0x00
#define DIB0700_REQ_I2C_READ            0x02
#define DIB0700_REQ_I2C_WRITE           0x03
#define DIB0700_REQ_JUMPRAM             0x08
#define DIB0700_REQ_SET_CLOCK           0x0B
#define DIB0700_REQ_SET_GPIO            0x0C
#define DIB0700_REQ_ENABLE_VIDEO        0x0F
#define DIB0700_REQ_SET_I2C_PARAM       0x10
#define DIB0700_REQ_NEW_I2C_READ        0x12
#define DIB0700_REQ_NEW_I2C_WRITE       0x13
#define DIB0700_REQ_GET_VERSION         0x15

/* USB control-transfer constants. */
#define DIB0700_USB_VENDOR_OUT  0x40   /* (USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_DEVICE) */
#define DIB0700_USB_VENDOR_IN   0xC0   /* (USB_DIR_IN |USB_TYPE_VENDOR|USB_RECIP_DEVICE) */
#define DIB0700_CTRL_TIMEOUT_MS 5000

/* USB bulk-OUT endpoint for firmware upload (matches upstream's
 * usb_sndbulkpipe(udev, 0x01)). */
#define DIB0700_FW_BULK_EP   0x01

struct dib0700_dev {
    usbq_dev_t       *usb;          /* not owned */

    /* Cached firmware-version word (zero until download succeeds). */
    uint32_t          fw_version;

    /* USB-side i2c-msg API switch. dib0700 firmware ≥ 1.20 uses the
     * "new" i2c protocol (REQUEST_NEW_I2C_*), older firmware uses the
     * "legacy" (REQUEST_I2C_*) one. Set by upper-layer code (xbox
     * device hint: fw_use_new_i2c_api = 1). */
    int               use_new_i2c;

    /* Per-bridge channel-state byte tracked by streaming_ctrl. The
     * REQUEST_ENABLE_VIDEO message needs the cumulative state of all
     * adapters on the bridge (bit 0 = adapter 0 streaming, bit 1 =
     * adapter 1, …). */
    uint8_t           channel_state;

    /* USB control-transfer scratch buffer. Sized to fit the largest
     * dib0700 i2c xfer (~64 bytes) plus the 4-byte header. */
    uint8_t           buf[256];

    /* Serializes ctrl/i2c access from multiple consumer threads
     * (e.g. an engine thread tuning while a status-poll thread reads
     * lock). */
    pthread_mutex_t   ctrl_lock;

    /* linuxdvbkpi i2c_adapter shim — handed to chip drivers. */
    struct i2c_adapter i2c_adap;
};

/* Single-shot vendor control transfers. tx/rx parameters mirror the
 * shape of upstream dib0700_ctrl_wr / dib0700_ctrl_rd. */
int dib0700_ctrl_wr(struct dib0700_dev *dev, uint8_t *tx, int txlen);
int dib0700_ctrl_rd(struct dib0700_dev *dev, uint8_t *tx, int txlen,
                    uint8_t *rx, int rxlen);

#endif
