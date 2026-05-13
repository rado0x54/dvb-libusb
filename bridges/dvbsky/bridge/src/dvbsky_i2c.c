/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvbsky i²c-over-USB master + linuxdvbkpi i2c_adapter shim.
 *
 * Direct re-expression of upstream's dvbsky_i2c_xfer (drivers/media/
 * usb/dvb-usb-v2/dvbsky.c). The bridge accepts three message shapes:
 *
 *   1 message, write:
 *      OUT: [0x08, addr, len, data...]   IN: [echo]
 *
 *   1 message, read:
 *      OUT: [0x09, 0, len, addr]         IN: [status, data..len bytes]
 *
 *   2 messages, write-then-read:
 *      OUT: [0x09, wlen, rlen, addr, wdata...]
 *      IN:  [status, rdata..rlen bytes]
 *
 * `num > 2` and per-message len > 60 are not supported (upstream's
 * cap, mirrored here).
 */

#include "dvbsky_priv.h"

#include "usbq/usbq.h"

#include <linux/i2c.h>

#include <errno.h>
#include <string.h>

static int dvbsky_i2c_one_write(struct dvbsky_dev *dev,
                                const struct i2c_msg *m) {
    if (m->len > DVBSKY_I2C_MAX_LEN) return -EOPNOTSUPP;
    uint8_t obuf[DVBSKY_BUF_LEN];
    obuf[0] = DVBSKY_OP_I2C_WRITE;
    obuf[1] = (uint8_t)m->addr;
    obuf[2] = (uint8_t)m->len;
    memcpy(&obuf[3], m->buf, m->len);
    uint8_t resp;
    return dvbsky_bulk_rw_locked(dev,
                                 obuf, (uint16_t)(m->len + 3),
                                 &resp, sizeof(resp));
}

static int dvbsky_i2c_one_read(struct dvbsky_dev *dev,
                               struct i2c_msg *m) {
    if (m->len > DVBSKY_I2C_MAX_LEN) return -EOPNOTSUPP;
    uint8_t obuf[4] = {
        DVBSKY_OP_I2C_READ, 0, (uint8_t)m->len, (uint8_t)m->addr,
    };
    uint8_t rbuf[DVBSKY_BUF_LEN];
    int rc = dvbsky_bulk_rw_locked(dev,
                                   obuf, sizeof(obuf),
                                   rbuf, (uint16_t)(m->len + 1));
    if (rc < 0) return rc;
    /* Upstream skips rbuf[0] (status echo) and returns rbuf[1..len]. */
    memcpy(m->buf, &rbuf[1], m->len);
    return 0;
}

static int dvbsky_i2c_write_then_read(struct dvbsky_dev *dev,
                                      const struct i2c_msg *w,
                                      struct i2c_msg *r) {
    if (w->len > DVBSKY_I2C_MAX_LEN || r->len > DVBSKY_I2C_MAX_LEN) {
        return -EOPNOTSUPP;
    }
    uint8_t obuf[DVBSKY_BUF_LEN];
    uint8_t rbuf[DVBSKY_BUF_LEN];
    obuf[0] = DVBSKY_OP_I2C_READ;
    obuf[1] = (uint8_t)w->len;
    obuf[2] = (uint8_t)r->len;
    obuf[3] = (uint8_t)w->addr;
    memcpy(&obuf[4], w->buf, w->len);
    int rc = dvbsky_bulk_rw_locked(dev,
                                   obuf, (uint16_t)(w->len + 4),
                                   rbuf, (uint16_t)(r->len + 1));
    if (rc < 0) return rc;
    memcpy(r->buf, &rbuf[1], r->len);
    return 0;
}

static int dvbsky_i2c_master_xfer(struct i2c_adapter *adap,
                                  struct i2c_msg *msgs, int num) {
    struct dvbsky_dev *dev = adap->algo_data;
    if (!dev) return -EIO;
    if (num <= 0 || num > 2) return -EOPNOTSUPP;

    pthread_mutex_lock(&dev->ctrl_lock);
    int rc;
    if (num == 1) {
        if (msgs[0].flags & I2C_M_RD) {
            rc = dvbsky_i2c_one_read(dev, &msgs[0]);
        } else {
            rc = dvbsky_i2c_one_write(dev, &msgs[0]);
        }
    } else {
        /* num == 2: upstream requires msg[0] = write, msg[1] = read,
         * same slave address. We don't try to be cleverer. */
        if ((msgs[0].flags & I2C_M_RD) || !(msgs[1].flags & I2C_M_RD)) {
            rc = -EOPNOTSUPP;
        } else {
            rc = dvbsky_i2c_write_then_read(dev, &msgs[0], &msgs[1]);
        }
    }
    pthread_mutex_unlock(&dev->ctrl_lock);
    return rc < 0 ? rc : num;
}

static u32 dvbsky_i2c_func(struct i2c_adapter *adap) {
    (void)adap;
    return I2C_FUNC_I2C;
}

const struct i2c_algorithm dvbsky_i2c_algo_userspace = {
    .master_xfer   = dvbsky_i2c_master_xfer,
    .functionality = dvbsky_i2c_func,
};
