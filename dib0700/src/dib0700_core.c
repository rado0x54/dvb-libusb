/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dib0700 bridge core — open/close + vendor control transfers + GPIO.
 * Direct re-expression of upstream's dib0700_core.c
 * (dib0700_ctrl_wr / dib0700_ctrl_rd / dib0700_set_gpio /
 * dib0700_set_i2c_speed / dib0700_get_version) over our usbq sync ops.
 */

#include "dib0700_priv.h"

#include "usbq/usbq.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations of the i2c shim wired here at open-time. */
extern const struct i2c_algorithm dib0700_i2c_algo_userland;

dib0700_dev_t *dib0700_open(usbq_dev_t *usb) {
    if (!usb) return NULL;
    dib0700_dev_t *dev = calloc(1, sizeof(*dev));
    if (!dev) return NULL;
    dev->usb = usb;
    pthread_mutex_init(&dev->ctrl_lock, NULL);

    /* Wire the linuxdvbkpi i2c_adapter shim. master_xfer in
     * dib0700_i2c.c will reach back into dev via algo_data. */
    dev->i2c_adap.algo      = &dib0700_i2c_algo_userland;
    dev->i2c_adap.algo_data = dev;
    snprintf(dev->i2c_adap.name, sizeof(dev->i2c_adap.name),
             "dib0700-i2c");
    snprintf(dev->i2c_adap.dev.name, sizeof(dev->i2c_adap.dev.name),
             "dib0700");
    /* algo_data and driver_data both point at us — chip driver code
     * doesn't actually inspect either, but i2c_get_adapdata() reads
     * driver_data. Set both to be safe. */
    dev->i2c_adap.driver_data = dev;

    return dev;
}

void dib0700_close(dib0700_dev_t *dev) {
    if (!dev) return;
    pthread_mutex_destroy(&dev->ctrl_lock);
    free(dev);
}

usbq_dev_t *dib0700_usb_handle(dib0700_dev_t *dev) {
    return dev ? dev->usb : NULL;
}

void *dib0700_get_i2c_adapter(dib0700_dev_t *dev) {
    return dev ? &dev->i2c_adap : NULL;
}

/* ---- vendor control transfers ---- */
/* Upstream's dib0700_ctrl_wr packs the request byte as bRequest and
 * sends `txlen` bytes (including the request byte) as the OUT data
 * stage. dib0700_ctrl_rd encodes the trailing tx bytes (after the
 * request) into wValue/wIndex per a fixed marshalling pattern. */

int dib0700_ctrl_wr(struct dib0700_dev *dev, uint8_t *tx, int txlen) {
    if (!dev || !tx || txlen < 1) return -EINVAL;
    int rc = usbq_control(dev->usb,
                          DIB0700_USB_VENDOR_OUT,
                          tx[0],            /* bRequest = req opcode */
                          0, 0,
                          tx, (uint16_t)txlen,
                          DIB0700_CTRL_TIMEOUT_MS);
    return rc < 0 ? rc : 0;
}

int dib0700_ctrl_rd(struct dib0700_dev *dev, uint8_t *tx, int txlen,
                    uint8_t *rx, int rxlen) {
    if (!dev || !tx || txlen < 2 || txlen > 4 || !rx || rxlen < 1) {
        return -EINVAL;
    }
    /* Same wValue / wIndex packing as upstream dib0700_ctrl_rd. */
    uint16_t value = (uint16_t)((txlen - 2) << 8) | tx[1];
    uint16_t index = 0;
    if (txlen > 2) index |= (uint16_t)(tx[2] << 8);
    if (txlen > 3) index |= tx[3];

    int rc = usbq_control(dev->usb,
                          DIB0700_USB_VENDOR_IN,
                          tx[0],
                          value, index,
                          rx, (uint16_t)rxlen,
                          DIB0700_CTRL_TIMEOUT_MS);
    return rc;
}

/* ---- GPIO + clock + i2c speed ---- */

int dib0700_set_gpio(dib0700_dev_t *dev, enum dib0700_gpio gpio,
                     int direction, int value) {
    if (!dev) return -EINVAL;
    pthread_mutex_lock(&dev->ctrl_lock);
    dev->buf[0] = DIB0700_REQ_SET_GPIO;
    dev->buf[1] = (uint8_t)gpio;
    dev->buf[2] = (uint8_t)(((direction & 0x01) << 7) |
                            ((value     & 0x01) << 6));
    int rc = dib0700_ctrl_wr(dev, dev->buf, 3);
    pthread_mutex_unlock(&dev->ctrl_lock);
    return rc;
}

int dib0700_set_i2c_speed(dib0700_dev_t *dev, uint16_t scl_kHz) {
    if (!dev || scl_kHz == 0) return -EINVAL;
    pthread_mutex_lock(&dev->ctrl_lock);
    dev->buf[0] = DIB0700_REQ_SET_I2C_PARAM;
    /* Same divider math as upstream dib0700_set_i2c_speed. */
    uint16_t d1 = (uint16_t)(30000u / scl_kHz);
    uint16_t d2 = (uint16_t)(72000u / scl_kHz);
    dev->buf[1] = 0;
    dev->buf[2] = (uint8_t)(d1 >> 8);
    dev->buf[3] = (uint8_t)(d1 & 0xff);
    dev->buf[4] = (uint8_t)(d2 >> 8);
    dev->buf[5] = (uint8_t)(d2 & 0xff);
    dev->buf[6] = (uint8_t)(d2 >> 8);
    dev->buf[7] = (uint8_t)(d2 & 0xff);
    int rc = dib0700_ctrl_wr(dev, dev->buf, 8);
    pthread_mutex_unlock(&dev->ctrl_lock);
    return rc;
}

/* ---- get_version + cold-detect ---- */

int dib0700_get_firmware_version(dib0700_dev_t *dev, uint32_t *out) {
    if (!dev || !out) return -EINVAL;
    uint8_t buf[16];
    int rc = usbq_control(dev->usb,
                          DIB0700_USB_VENDOR_IN,
                          DIB0700_REQ_GET_VERSION,
                          0, 0,
                          buf, sizeof(buf),
                          DIB0700_CTRL_TIMEOUT_MS);
    if (rc < 16) return rc < 0 ? rc : -EIO;
    /* Upstream packs hwversion / romversion / ramversion / fwtype into
     * 4-byte big-endian fields. The "running firmware" version is the
     * ramversion (offset 8). */
    *out = ((uint32_t)buf[8]  << 24) | ((uint32_t)buf[9]  << 16) |
           ((uint32_t)buf[10] << 8)  | (uint32_t)buf[11];
    dev->fw_version = *out;
    return 0;
}

int dib0700_is_cold(dib0700_dev_t *dev) {
    if (!dev) return -EINVAL;
    uint8_t buf[16];
    int rc = usbq_control(dev->usb,
                          DIB0700_USB_VENDOR_IN,
                          DIB0700_REQ_GET_VERSION,
                          0, 0,
                          buf, sizeof(buf),
                          DIB0700_CTRL_TIMEOUT_MS);
    /* Cold device: GET_VERSION fails or returns zero bytes (per
     * upstream dib0700_identify_state). */
    return rc <= 0 ? 1 : 0;
}
