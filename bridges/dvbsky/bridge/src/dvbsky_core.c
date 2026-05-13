/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvbsky bridge core — open/close + bulk command/response + GPIO.
 * Direct re-expression of upstream's dvbsky_usb_generic_rw and
 * dvbsky_gpio_ctrl over our usbq bulk ops.
 *
 * The bridge uses bulk endpoints (EP 0x01 OUT for commands,
 * EP 0x81 IN for responses) instead of vendor control transfers
 * — that's why we route through usbq_bulk_write / usbq_bulk_read
 * here, in contrast to dib0700_core.c which uses usbq_control.
 */

#include "dvbsky_priv.h"

#include "usbq/usbq.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations of the i2c shim wired here at open-time. */
extern const struct i2c_algorithm dvbsky_i2c_algo_userspace;

dvbsky_dev_t *dvbsky_open(usbq_dev_t *usb) {
    if (!usb) return NULL;
    dvbsky_dev_t *dev = calloc(1, sizeof(*dev));
    if (!dev) return NULL;
    dev->usb = usb;
    pthread_mutex_init(&dev->ctrl_lock, NULL);

    /* Wire the linuxdvbkpi i2c_adapter shim. master_xfer in
     * dvbsky_i2c.c will reach back into dev via algo_data. */
    dev->i2c_adap.algo      = &dvbsky_i2c_algo_userspace;
    dev->i2c_adap.algo_data = dev;
    snprintf(dev->i2c_adap.name, sizeof(dev->i2c_adap.name),
             "dvbsky-i2c");
    snprintf(dev->i2c_adap.dev.name, sizeof(dev->i2c_adap.dev.name),
             "dvbsky");
    dev->i2c_adap.driver_data = dev;

    return dev;
}

void dvbsky_close(dvbsky_dev_t *dev) {
    if (!dev) return;
    pthread_mutex_destroy(&dev->ctrl_lock);
    free(dev);
}

usbq_dev_t *dvbsky_usb_handle(dvbsky_dev_t *dev) {
    return dev ? dev->usb : NULL;
}

void *dvbsky_get_i2c_adapter(dvbsky_dev_t *dev) {
    return dev ? &dev->i2c_adap : NULL;
}

/* ---- bulk command/response (generic_rw analogue) ---- */

int dvbsky_bulk_rw_locked(struct dvbsky_dev *dev,
                          const uint8_t *wbuf, uint16_t wlen,
                          uint8_t *rbuf, uint16_t rlen) {
    if (!dev || (wlen > 0 && !wbuf) || (rlen > 0 && !rbuf)) return -EINVAL;
    if (wlen > sizeof(dev->obuf) || rlen > sizeof(dev->ibuf)) return -EINVAL;

    if (wlen > 0) {
        memcpy(dev->obuf, wbuf, wlen);
        int n = usbq_bulk_write(dev->usb, DVBSKY_CMD_EP_OUT,
                                dev->obuf, wlen,
                                DVBSKY_BULK_TIMEOUT_MS);
        if (n < 0) return n;
        if ((uint16_t)n != wlen) return -EIO;
    }
    if (rlen > 0) {
        int n = usbq_bulk_read(dev->usb, DVBSKY_CMD_EP_IN,
                               dev->ibuf, rlen,
                               DVBSKY_BULK_TIMEOUT_MS);
        if (n < 0) return n;
        if ((uint16_t)n < rlen) return -EIO;
        memcpy(rbuf, dev->ibuf, rlen);
    }
    return 0;
}

int dvbsky_bulk_rw(struct dvbsky_dev *dev,
                   const uint8_t *wbuf, uint16_t wlen,
                   uint8_t *rbuf, uint16_t rlen) {
    pthread_mutex_lock(&dev->ctrl_lock);
    int rc = dvbsky_bulk_rw_locked(dev, wbuf, wlen, rbuf, rlen);
    pthread_mutex_unlock(&dev->ctrl_lock);
    return rc;
}

/* ---- GPIO ---- */

int dvbsky_gpio_ctrl(dvbsky_dev_t *dev, uint8_t gport, uint8_t value) {
    if (!dev) return -EINVAL;
    uint8_t cmd[3] = { DVBSKY_OP_GPIO, gport, value };
    uint8_t resp;
    return dvbsky_bulk_rw(dev, cmd, sizeof(cmd), &resp, sizeof(resp));
}
