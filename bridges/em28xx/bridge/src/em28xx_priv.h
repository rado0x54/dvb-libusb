/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Internal definitions shared between em28xx_core.c and em28xx_i2c.c.
 * Not part of the public API.
 */

#ifndef EM28XX_PRIV_H
#define EM28XX_PRIV_H

#include "em28xx/em28xx.h"

#include <linux/i2c.h>

/* Upstream's URB_MAX_CTRL_SIZE. The kernel reuses one mmap'd 64-byte
 * scratch buffer (`dev->urb_buf`) per device for both directions.
 * libusb manages its own per-transfer buffers so we don't need to
 * mirror the allocation pattern, but the upper-length bound applies
 * regardless — it's a hardware constraint of the bridge's vendor
 * control-transfer implementation. */
#define EM28XX_URB_MAX_CTRL_SIZE 64

/* Per-bus algo_data backing the linuxdvbkpi i2c_adapter shim handed
 * to upstream chip drivers (lifted si2168/si2157). Lives inside
 * em28xx_dev so its lifetime matches the bridge handle. */
struct em28xx_i2c_bus_ctx {
    em28xx_dev_t *dev;
    int           bus;
};

struct em28xx_dev {
    usbq_dev_t *usb;        /* not owned */
    int          chip_id;    /* 0 if unread; otherwise EM28XX_CHIP_ID_* */

    /* I²C state. */
    uint8_t      i2c_speed;     /* shadow of EM28XX_R06_I2C_CLK */
    int          cur_i2c_bus;   /* -1 = unknown, else 0 / 1. */

    /* linuxdvbkpi adapter shims, lazy-initialized per bus on first
     * em28xx_get_i2c_adapter() call. Two buses; we cache both. */
    struct i2c_adapter         i2c_adap[2];
    struct em28xx_i2c_bus_ctx  i2c_ctx[2];
};

static inline usbq_dev_t *em28xx_usb(em28xx_dev_t *dev) {
    return dev->usb;
}

#endif /* EM28XX_PRIV_H */
