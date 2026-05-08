/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * em28xx core: register R/W over USB control transfers.
 *
 * Direct port of em28xx_read_reg_req_len / em28xx_write_regs_req
 * from drivers/media/usb/em28xx/em28xx-core.c
 * (torvalds/linux @ 9207d47f).
 *
 * The kernel uses bRequest = USB_REQ_GET_STATUS (0x00) for both
 * directions, with bmRequestType = (USB_DIR_{IN,OUT} | USB_TYPE_VENDOR
 * | USB_RECIP_DEVICE), wValue = 0, wIndex = register address. We
 * pass these values verbatim to usbq_control. The "abuse
 * GET_STATUS as a vendor request" is on Empia's side, not ours;
 * preserving the upstream behaviour byte-for-byte is the point.
 */

#include "em28xx_priv.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* USB constants from <linux/usb/ch9.h>. Ducking the libusb header here
 * to keep the public surface limited to usbq. */
#define EM28XX_USB_REQ_GET_STATUS  0x00
#define EM28XX_BMRT_IN_VENDOR_DEV  0xC0  /* USB_DIR_IN  | USB_TYPE_VENDOR | USB_RECIP_DEVICE */
#define EM28XX_BMRT_OUT_VENDOR_DEV 0x40  /* USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE */

/* Upstream uses a 1000 ms timeout for control transfers. Match it. */
#define EM28XX_CTRL_TIMEOUT_MS 1000

/* struct em28xx_dev is defined in em28xx_priv.h. */

/* ---- Implementations ---- */

int em28xx_read_reg_req_len(em28xx_dev_t *dev, uint8_t req, uint16_t reg,
                            void *buf, int len) {
    if (!dev || !buf || len <= 0 || len > EM28XX_URB_MAX_CTRL_SIZE) {
        return -EINVAL;
    }
    int ret = usbq_control(em28xx_usb(dev),
                                EM28XX_BMRT_IN_VENDOR_DEV,
                                req,
                                /*wValue=*/0x0000,
                                /*wIndex=*/reg,
                                buf,
                                /*wLength=*/(uint16_t)len,
                                EM28XX_CTRL_TIMEOUT_MS);
    return ret;
}

int em28xx_write_regs_req(em28xx_dev_t *dev, uint8_t req, uint16_t reg,
                          const void *buf, int len) {
    if (!dev || !buf || len < 1 || len > EM28XX_URB_MAX_CTRL_SIZE) {
        return -EINVAL;
    }
    /* usbq_control's `buf` is non-const because for IN transfers
     * it's an output. For OUT it's read-only — cast away const,
     * mirroring libusb_control_transfer's same compromise. */
    int ret = usbq_control(em28xx_usb(dev),
                                EM28XX_BMRT_OUT_VENDOR_DEV,
                                req,
                                /*wValue=*/0x0000,
                                /*wIndex=*/reg,
                                (void *)buf,
                                /*wLength=*/(uint16_t)len,
                                EM28XX_CTRL_TIMEOUT_MS);
    return ret;
}

int em28xx_read_reg_len(em28xx_dev_t *dev, uint16_t reg, void *buf, int len) {
    return em28xx_read_reg_req_len(dev, EM28XX_USB_REQ_GET_STATUS, reg,
                                   buf, len);
}

int em28xx_read_reg(em28xx_dev_t *dev, uint16_t reg) {
    uint8_t v = 0;
    int ret = em28xx_read_reg_len(dev, reg, &v, 1);
    if (ret < 0) {
        return ret;
    }
    return v;
}

int em28xx_write_regs(em28xx_dev_t *dev, uint16_t reg,
                      const void *buf, int len) {
    int ret = em28xx_write_regs_req(dev, EM28XX_USB_REQ_GET_STATUS, reg,
                                    buf, len);
    return ret < 0 ? ret : 0;
}

int em28xx_write_reg(em28xx_dev_t *dev, uint16_t reg, uint8_t val) {
    return em28xx_write_regs(dev, reg, &val, 1);
}

int em28xx_write_reg_bits(em28xx_dev_t *dev, uint16_t reg,
                          uint8_t val, uint8_t bitmask) {
    int oldval = em28xx_read_reg(dev, reg);
    if (oldval < 0) {
        return oldval;
    }
    uint8_t newval = ((uint8_t)oldval & (uint8_t)~bitmask) | (val & bitmask);
    return em28xx_write_regs(dev, reg, &newval, 1);
}

/* ---- Lifecycle ---- */

em28xx_dev_t *em28xx_open(usbq_dev_t *usb) {
    if (!usb) {
        return NULL;
    }
    em28xx_dev_t *dev = (em28xx_dev_t *)calloc(1, sizeof(*dev));
    if (!dev) {
        return NULL;
    }
    dev->usb = usb;
    dev->cur_i2c_bus = -1;  /* unknown until first em28xx_i2c_xfer */
    dev->i2c_speed = 0;     /* set via em28xx_i2c_set_speed */

    /* Probe the chip-ID register. Upstream em28xx_init_dev() does
     * exactly this in em28xx-cards.c around line 3800. We don't gate
     * open() on a known chip ID — the caller decides whether the
     * value is acceptable for its workflow — but caching it here
     * means the typical call site (`em28xx_open(); em28xx_chip_id()`)
     * doesn't have to deal with the read failure mode separately. */
    int chip = em28xx_read_reg(dev, EM28XX_R0A_CHIPID);
    if (chip < 0) {
        dev->chip_id = 0;
    } else {
        dev->chip_id = chip;
    }
    return dev;
}

void em28xx_close(em28xx_dev_t *dev) {
    if (!dev) {
        return;
    }
    /* dev->usb is borrowed; not our job to close it. */
    free(dev);
}

int em28xx_chip_id(const em28xx_dev_t *dev) {
    return dev ? dev->chip_id : 0;
}

usbq_dev_t *em28xx_usb_handle(em28xx_dev_t *dev) {
    return dev ? dev->usb : NULL;
}
