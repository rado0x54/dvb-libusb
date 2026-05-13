/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DVBSKY_PRIV_H
#define DVBSKY_PRIV_H

#include "dvbsky/dvbsky.h"

#include <linux/i2c.h>
#include <pthread.h>
#include <stdint.h>

/* dvbsky USB command opcodes (see upstream dvbsky.c).
 *
 * The bridge speaks a tiny framed protocol over bulk EP 0x01 / 0x81.
 * Each command frame starts with one opcode byte; arguments depend
 * on the opcode. Responses (when present) come back on EP 0x81 with
 * a leading status byte (we discard it on success). */
#define DVBSKY_OP_I2C_WRITE     0x08  /* [0x08, addr, len, data...]                 -> [echo]              */
#define DVBSKY_OP_I2C_READ      0x09  /* [0x09, wlen, rlen, addr, wdata...]         -> [status, rdata...]  */
#define DVBSKY_OP_GPIO          0x0e  /* [0x0e, gport, value]                       -> [status]            */
#define DVBSKY_OP_STREAM_START  0x36  /* [0x36, 3, 0]                               -> (none expected)     */
#define DVBSKY_OP_STREAM_STOP   0x37  /* [0x37, 0, 0]                               -> (none expected)     */

/* Bulk endpoints for the command protocol. The TS stream uses
 * DVBSKY_TS_BULK_EP (0x82), declared in the public header so the
 * engine can wire it into usbq_stream_open without including this
 * private header. */
#define DVBSKY_CMD_EP_OUT       0x01
#define DVBSKY_CMD_EP_IN        0x81

/* Max bulk frame size used by upstream (DVBSKY_BUF_LEN). The bridge
 * tops out at 60 i²c data bytes per frame, plus a few opcode bytes. */
#define DVBSKY_BUF_LEN          64
#define DVBSKY_I2C_MAX_LEN      60

/* USB bulk timeout for command frames. Upstream defaults to the
 * dvb-usb-v2 framework's 2000 ms; mirrored here. */
#define DVBSKY_BULK_TIMEOUT_MS  2000

struct dvbsky_dev {
    usbq_dev_t       *usb;          /* not owned */

    /* Scratch buffers used by command/response. Separate so a write
     * can be in flight while a read response is staging — though
     * `ctrl_lock` currently serializes everything. */
    uint8_t           obuf[DVBSKY_BUF_LEN];
    uint8_t           ibuf[DVBSKY_BUF_LEN];

    /* Serializes ctrl/i²c access from multiple consumer threads
     * (engine thread tuning, status-poll thread reading lock). */
    pthread_mutex_t   ctrl_lock;

    /* linuxdvbkpi i²c_adapter shim — handed to chip drivers. */
    struct i2c_adapter i2c_adap;
};

/* Single-shot command/response over the bulk-out/bulk-in pair.
 * Mirrors upstream dvbsky_usb_generic_rw. `rlen == 0` is a pure
 * write. The caller is expected to be holding ctrl_lock. */
int dvbsky_bulk_rw_locked(struct dvbsky_dev *dev,
                          const uint8_t *wbuf, uint16_t wlen,
                          uint8_t *rbuf, uint16_t rlen);

/* Convenience: lock + bulk_rw + unlock. */
int dvbsky_bulk_rw(struct dvbsky_dev *dev,
                   const uint8_t *wbuf, uint16_t wlen,
                   uint8_t *rbuf, uint16_t rlen);

#endif
