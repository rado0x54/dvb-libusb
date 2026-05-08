/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * em28xx I²C-over-USB master.
 *
 * Direct port of the EM28XX_I2C_ALGO_EM28XX paths in
 * drivers/media/usb/em28xx/em28xx-i2c.c
 * (torvalds/linux @ 9207d47f).
 *
 * Scope: only EM28XX_I2C_ALGO_EM28XX is implemented — the algorithm
 * every em28174 / em28178 board uses, including the WinTV-dualHD.
 * The em2800 and em25xx_bus_b algos are out of scope; they're for
 * older / cheaper bridges we don't target.
 *
 * Wire shape (per upstream):
 *   Write: USB control OUT, bRequest = 2 (stop) or 3 (no stop),
 *          wIndex = (slave_addr << 1), payload = data bytes.
 *   Read:  USB control IN,  bRequest = 2,
 *          wIndex = (slave_addr << 1), wLength = byte count.
 *
 * After every transfer, register 0x05 reports the bridge-side ACK
 * status:
 *   0x00 = success
 *   0x10 = NAK from slave  → -ENXIO
 *   0x02 / 0x04 = clock-stretch timeout → -ETIMEDOUT
 * The kernel polls 0x05 every 5 ms within a 36 ms window
 * (35 ms base + 1 ms for 100/400 kHz buses); we match that.
 *
 * Bus selection: the em28xx exposes two I²C buses, switched by a
 * bit (EM2874_I2C_SECONDARY_BUS_SELECT, 0x04) in the I2C_CLK
 * register (0x06). The WinTV-dualHD's demod sits on bus 1.
 */

#include "em28xx_priv.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* bRequest values used by the i2c algo. The bridge re-purposes the
 * bRequest byte: 2 = "stop after this", 3 = "no stop / continued
 * write". The reg-access path uses 0 (USB_REQ_GET_STATUS) — see
 * em28xx_core.c. */
#define EM28XX_I2C_REQ_STOP    2
#define EM28XX_I2C_REQ_NO_STOP 3

/* Status register the bridge updates after each i2c xfer. Defined
 * inline rather than in the public header — it's an internal
 * implementation detail of the bridge's i2c state machine. */
#define EM28XX_R05_I2C_STATUS 0x05

/* Bridge-side i2c-status decode. */
#define EM28XX_I2C_STATUS_OK            0x00
#define EM28XX_I2C_STATUS_NAK           0x10
#define EM28XX_I2C_STATUS_TIMEOUT_A     0x02
#define EM28XX_I2C_STATUS_TIMEOUT_B     0x04

/* Per em28xx_i2c_timeout(): 35 ms base + 1 ms slack at 100/400 kHz.
 * Kept slack-tolerant — nothing is real-time critical here. */
#define EM28XX_I2C_XFER_TIMEOUT_MS 36

/* Polling interval matching the kernel's usleep_range(5000, 6000). */
#define EM28XX_I2C_POLL_INTERVAL_US 5000

static void em28xx_msleep_us(unsigned us) {
    struct timespec ts = {
        .tv_sec  = (time_t)(us / 1000000u),
        .tv_nsec = (long)((us % 1000000u) * 1000u),
    };
    /* nanosleep can return early with EINTR. We don't care: the
     * outer loop's deadline check handles it. */
    nanosleep(&ts, NULL);
}

static long em28xx_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + (long)(ts.tv_nsec / 1000000L);
}

/* Read EM28XX_R05_I2C_STATUS until success / fatal error / timeout.
 * Mirrors the polling loop in em28xx_i2c_send_bytes upstream. */
static int em28xx_i2c_wait_status(em28xx_dev_t *dev) {
    long deadline = em28xx_now_ms() + EM28XX_I2C_XFER_TIMEOUT_MS;
    int last_status = -EIO;

    for (;;) {
        int s = em28xx_read_reg(dev, EM28XX_R05_I2C_STATUS);
        if (s < 0) {
            return s;
        }
        last_status = s;
        if (s == EM28XX_I2C_STATUS_OK) {
            return 0;
        }
        if (s == EM28XX_I2C_STATUS_NAK) {
            return -ENXIO;
        }
        if (em28xx_now_ms() >= deadline) {
            break;
        }
        em28xx_msleep_us(EM28XX_I2C_POLL_INTERVAL_US);
    }

    if (last_status == EM28XX_I2C_STATUS_TIMEOUT_A ||
        last_status == EM28XX_I2C_STATUS_TIMEOUT_B) {
        return -ETIMEDOUT;
    }
    return -EIO;
}

/* Single-message transfer helpers. Upstream calls these
 * em28xx_i2c_send_bytes / em28xx_i2c_recv_bytes. */

static int em28xx_i2c_send_bytes(em28xx_dev_t *dev, uint16_t addr,
                                 const uint8_t *buf, uint16_t len,
                                 int stop) {
    if (len < 1 || len > EM28XX_URB_MAX_CTRL_SIZE) {
        return -EOPNOTSUPP;
    }
    uint8_t req = stop ? EM28XX_I2C_REQ_STOP : EM28XX_I2C_REQ_NO_STOP;
    int ret = em28xx_write_regs_req(dev, req, addr, buf, len);
    if (ret < 0) {
        return ret;
    }
    if (ret != (int)len) {
        return -EIO;
    }
    int s = em28xx_i2c_wait_status(dev);
    if (s < 0) {
        return s;
    }
    return (int)len;
}

static int em28xx_i2c_recv_bytes(em28xx_dev_t *dev, uint16_t addr,
                                 uint8_t *buf, uint16_t len) {
    if (len < 1 || len > EM28XX_URB_MAX_CTRL_SIZE) {
        return -EOPNOTSUPP;
    }
    int ret = em28xx_read_reg_req_len(dev, EM28XX_I2C_REQ_STOP, addr,
                                      buf, len);
    if (ret < 0) {
        return ret;
    }
    /* Note: upstream observes that on bus 1 with no prior write to
     * the slave, a missing device can return 0 bytes instead of an
     * error. The wait_status check below catches that case via the
     * NAK status byte, so we don't need a special-case for it. */
    int s = em28xx_i2c_wait_status(dev);
    if (s < 0) {
        return s;
    }
    return ret;
}

static int em28xx_i2c_check_for_device(em28xx_dev_t *dev, uint16_t addr) {
    uint8_t b;
    int ret = em28xx_i2c_recv_bytes(dev, addr, &b, 1);
    if (ret == 1) {
        return 0;
    }
    return ret < 0 ? ret : -EIO;
}

/* ---- Public API ---- */

int em28xx_i2c_set_speed(em28xx_dev_t *dev, uint8_t i2c_clk_reg_val) {
    if (!dev) {
        return -EINVAL;
    }
    int ret = em28xx_write_reg(dev, EM28XX_R06_I2C_CLK, i2c_clk_reg_val);
    if (ret < 0) {
        return ret;
    }
    /* Mask off the bus-select bit before we shadow it — that bit is
     * managed by em28xx_i2c_xfer below, not by the speed setter.
     * Also keep cur_i2c_bus consistent with whichever bus the new
     * value selects. */
    dev->i2c_speed = i2c_clk_reg_val & (uint8_t)~EM2874_I2C_SECONDARY_BUS_SELECT;
    dev->cur_i2c_bus = (i2c_clk_reg_val & EM2874_I2C_SECONDARY_BUS_SELECT)
                       ? 1 : 0;
    return 0;
}

/* ---- linuxdvbkpi i2c_adapter shim ----
 *
 * Hands a `struct i2c_adapter` to upstream chip drivers (lifted
 * si2168 / si2157) so they can call `i2c_transfer` against the
 * em28xx bridge as if it were a kernel-side i2c bus.
 *
 * em28xx_i2c_msg_t and `struct i2c_msg` (linuxdvbkpi) have identical
 * memory layout (addr / flags / len / buf — same widths, same
 * order, EM28XX_I2C_M_RD == I2C_M_RD == 0x0001). We cast rather
 * than copying. */

static int em28xx_kpi_master_xfer(struct i2c_adapter *adap,
                                  struct i2c_msg *msgs, int num) {
    struct em28xx_i2c_bus_ctx *ctx = adap->algo_data;
    if (!ctx) return -EIO;
    return em28xx_i2c_xfer(ctx->dev, ctx->bus,
                           (em28xx_i2c_msg_t *)msgs, num);
}

static u32 em28xx_kpi_func(struct i2c_adapter *adap) {
    (void)adap;
    return I2C_FUNC_I2C;
}

static const struct i2c_algorithm em28xx_kpi_algo = {
    .master_xfer   = em28xx_kpi_master_xfer,
    .functionality = em28xx_kpi_func,
};

struct i2c_adapter *em28xx_get_i2c_adapter(em28xx_dev_t *dev, int bus) {
    if (!dev || (bus != 0 && bus != 1)) return NULL;
    struct i2c_adapter *adap = &dev->i2c_adap[bus];
    if (!adap->algo) {
        dev->i2c_ctx[bus].dev = dev;
        dev->i2c_ctx[bus].bus = bus;
        adap->algo      = &em28xx_kpi_algo;
        adap->algo_data = &dev->i2c_ctx[bus];
        snprintf(adap->name, sizeof(adap->name), "em28xx-bus%d", bus);
    }
    return adap;
}

int em28xx_i2c_xfer(em28xx_dev_t *dev, int bus,
                    em28xx_i2c_msg_t *msgs, int num) {
    if (!dev || !msgs || num < 0) {
        return -EINVAL;
    }
    if (bus != 0 && bus != 1) {
        return -EINVAL;
    }

    /* Switch I²C bus if needed. Mirrors the upstream "switch bus
     * before xfer" stanza. We track the current bus in
     * dev->cur_i2c_bus to avoid pointless writes. */
    if (bus != dev->cur_i2c_bus) {
        uint8_t bus_bit = (bus == 1) ? EM2874_I2C_SECONDARY_BUS_SELECT : 0;
        int ret = em28xx_write_reg_bits(dev, EM28XX_R06_I2C_CLK,
                                        bus_bit,
                                        EM2874_I2C_SECONDARY_BUS_SELECT);
        if (ret < 0) {
            return ret;
        }
        dev->cur_i2c_bus = bus;
    }

    for (int i = 0; i < num; i++) {
        em28xx_i2c_msg_t *m = &msgs[i];
        /* Slave address is shifted left 1 bit on the wire, matching
         * the standard I²C 7-bit-addr-with-RW-bit convention.
         * Upstream does the same shift in i2c_master_xfer. */
        uint16_t addr_w = (uint16_t)(m->addr << 1);
        int rc;

        if (m->len == 0) {
            /* Probe-only: caller wants to know if anything's there. */
            rc = em28xx_i2c_check_for_device(dev, addr_w);
        } else if (m->flags & EM28XX_I2C_M_RD) {
            rc = em28xx_i2c_recv_bytes(dev, addr_w, m->buf, m->len);
            if (rc >= 0 && rc != m->len) {
                /* Upstream tolerates short reads (just dprintk's a
                 * dbg line); we surface a short read as -EIO so the
                 * caller doesn't silently get partial data. */
                rc = -EIO;
            } else if (rc == m->len) {
                rc = 0;
            }
        } else {
            int stop = (i == num - 1);
            rc = em28xx_i2c_send_bytes(dev, addr_w, m->buf, m->len, stop);
            if (rc == m->len) {
                rc = 0;
            } else if (rc >= 0) {
                rc = -EIO;
            }
        }

        if (rc < 0) {
            return rc;
        }
    }

    return num;
}
