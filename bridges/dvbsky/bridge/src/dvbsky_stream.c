/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvbsky streaming control — flips the bulk-IN TS endpoint.
 *
 * Upstream's `dvbsky_stream_ctrl`:
 *   off: write [0x37, 0, 0]
 *   on:  write [0x37, 0, 0]; msleep(20); write [0x36, 3, 0]
 *
 * The "stop first" pre-roll on the on-path is upstream's idiom; the
 * slave fifo needs to be reset before the enable side takes. The
 * engine also re-fires the on-path each time the demod transitions
 * from unlocked → locked (per upstream's `dvbsky_usb_read_status`
 * "resync the slave fifo when signal locks"), but that lock-edge
 * detection lives in the engine layer — this primitive is the raw
 * toggle.
 */

#include "dvbsky_priv.h"

#include <errno.h>
#include <time.h>

static void msleep_local(unsigned ms) {
    if (!ms) return;
    struct timespec ts = {
        .tv_sec  = (time_t)(ms / 1000u),
        .tv_nsec = (long)((ms % 1000u) * 1000000UL),
    };
    nanosleep(&ts, NULL);
}

int dvbsky_stream_ctrl(dvbsky_dev_t *dev, int onoff) {
    if (!dev) return -EINVAL;
    static const uint8_t cmd_stop[3]  = { DVBSKY_OP_STREAM_STOP,  0, 0 };
    static const uint8_t cmd_start[3] = { DVBSKY_OP_STREAM_START, 3, 0 };

    pthread_mutex_lock(&dev->ctrl_lock);
    int rc = 0;
    /* upstream issues the stop frame unconditionally — both on the
     * disable path and as the pre-roll for the enable path. */
    {
        int n = usbq_bulk_write(dev->usb, DVBSKY_CMD_EP_OUT,
                                cmd_stop, sizeof(cmd_stop),
                                DVBSKY_BULK_TIMEOUT_MS);
        if (n < 0) { rc = n; goto out; }
        if (n != (int)sizeof(cmd_stop)) { rc = -EIO; goto out; }
    }
    if (onoff) {
        msleep_local(20);
        int n = usbq_bulk_write(dev->usb, DVBSKY_CMD_EP_OUT,
                                cmd_start, sizeof(cmd_start),
                                DVBSKY_BULK_TIMEOUT_MS);
        if (n < 0) { rc = n; goto out; }
        if (n != (int)sizeof(cmd_start)) { rc = -EIO; goto out; }
    }
out:
    pthread_mutex_unlock(&dev->ctrl_lock);
    return rc;
}
