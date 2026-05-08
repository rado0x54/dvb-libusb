/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dib0700 i²c-over-USB master + linuxdvbkpi i2c_adapter shim.
 *
 * Direct re-expression of upstream's dib0700_i2c_xfer_new (the only
 * variant we need — the xbox tuner runs firmware ≥ 1.20 and the
 * upstream xbox_one_attach sets fw_use_new_i2c_api = 1 unconditionally).
 *
 * Wire format for REQUEST_NEW_I2C_READ:
 *   bRequest = 0x12
 *   wValue   = (en_start<<7 | en_stop<<6 | (len & 0x3f)) << 8 | (addr<<1)
 *   wIndex   = (gen_mode<<6 & 0xC0) | (bus_mode<<4 & 0x30)
 *   IN data  = `len` bytes of payload
 *
 * Wire format for REQUEST_NEW_I2C_WRITE:
 *   bRequest = 0x13
 *   wValue   = 0
 *   wIndex   = 0
 *   OUT data = [0x13, addr<<1, en_start<<7|en_stop<<6|(len&0x3f),
 *               (gen_mode<<6|bus_mode<<4)] + payload
 *
 * gen_mode = 0 (master i2c), bus_mode = 1 (frontend bus).
 */

#include "dib0700_priv.h"

#include "usbq/usbq.h"

#include <linux/i2c.h>

#include <errno.h>
#include <string.h>

#define I2C_BUS_FE  1
#define I2C_GEN_MASTER 0

static int dib0700_i2c_xfer_one_read(struct dib0700_dev *dev,
                                     struct i2c_msg *m,
                                     int en_start, int en_stop) {
    uint16_t value = (uint16_t)(((en_start << 7) | (en_stop << 6) |
                                 (m->len & 0x3f)) << 8 |
                                (m->addr << 1));
    uint16_t index = (uint16_t)(((I2C_GEN_MASTER << 6) & 0xC0) |
                                ((I2C_BUS_FE << 4) & 0x30));
    int rc = usbq_control(dev->usb,
                          DIB0700_USB_VENDOR_IN,
                          DIB0700_REQ_NEW_I2C_READ,
                          value, index,
                          dev->buf, m->len,
                          DIB0700_CTRL_TIMEOUT_MS);
    if (rc < 0) return rc;
    if (rc < m->len) return -EIO;
    memcpy(m->buf, dev->buf, m->len);
    return 0;
}

static int dib0700_i2c_xfer_one_write(struct dib0700_dev *dev,
                                      struct i2c_msg *m,
                                      int en_start, int en_stop) {
    if ((size_t)m->len > sizeof(dev->buf) - 4) {
        return -EOPNOTSUPP;
    }
    dev->buf[0] = DIB0700_REQ_NEW_I2C_WRITE;
    dev->buf[1] = (uint8_t)(m->addr << 1);
    dev->buf[2] = (uint8_t)((en_start << 7) | (en_stop << 6) |
                            (m->len & 0x3f));
    dev->buf[3] = (uint8_t)(((I2C_GEN_MASTER << 6) & 0xC0) |
                            ((I2C_BUS_FE << 4) & 0x30));
    memcpy(&dev->buf[4], m->buf, m->len);

    int rc = usbq_control(dev->usb,
                          DIB0700_USB_VENDOR_OUT,
                          DIB0700_REQ_NEW_I2C_WRITE,
                          0, 0,
                          dev->buf, (uint16_t)(m->len + 4),
                          DIB0700_CTRL_TIMEOUT_MS);
    return rc < 0 ? rc : 0;
}

static int dib0700_i2c_master_xfer(struct i2c_adapter *adap,
                                   struct i2c_msg *msgs, int num) {
    struct dib0700_dev *dev = adap->algo_data;
    if (!dev) return -EIO;

    pthread_mutex_lock(&dev->ctrl_lock);
    int i, rc = 0;
    for (i = 0; i < num; i++) {
        int en_start = (i == 0) || !(msgs[i].flags & I2C_M_NOSTART);
        int en_stop  = (i == num - 1);
        if (msgs[i].flags & I2C_M_RD) {
            rc = dib0700_i2c_xfer_one_read(dev, &msgs[i], en_start, en_stop);
        } else {
            rc = dib0700_i2c_xfer_one_write(dev, &msgs[i], en_start, en_stop);
        }
        if (rc < 0) break;
    }
    pthread_mutex_unlock(&dev->ctrl_lock);
    return rc < 0 ? rc : i;
}

static u32 dib0700_i2c_func(struct i2c_adapter *adap) {
    (void)adap;
    return I2C_FUNC_I2C;
}

const struct i2c_algorithm dib0700_i2c_algo_userspace = {
    .master_xfer   = dib0700_i2c_master_xfer,
    .functionality = dib0700_i2c_func,
};
