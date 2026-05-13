/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Userspace port of drivers/media/usb/dvb-usb-v2/dvbsky.c (the
 * "dvbsky" USB bridge used by DVBSky S960/T680C/T330, the Mygica
 * T230 family, the Geniatech EyeTV Stick, and a few other OEM
 * rebrands). Same shape as dib0700/ / em28xx/ — open/close on top
 * of a usbq device, an i²c-over-USB master, GPIO control, and a
 * streaming-control call that flips the bulk-IN TS endpoint on
 * and off.
 *
 * The bridge does NOT need a firmware upload (the chip ships in
 * "warm" state — dmesg reports "found ... in warm state"); the only
 * cold-side init is a per-board GPIO power/reset sequence, handled
 * by the engine layer.
 *
 * License: GPL-2.0-or-later. The protocol is upstream's; this file
 * is a clean re-expression in our codebase shape.
 *
 * Wire layout (per upstream dvbsky.c):
 *   Endpoint 0x01 (bulk OUT) — command frame, up to 64 bytes.
 *   Endpoint 0x81 (bulk IN)  — response frame, up to 64 bytes.
 *   Endpoint 0x82 (bulk IN)  — MPEG TS, 4096-byte URBs, pool depth 8.
 *
 * Opcodes the upper layers actually need (see dvbsky_priv.h for the
 * full table):
 *   0x08 — i²c write
 *   0x09 — i²c read / write-then-read
 *   0x0e — GPIO write
 *   0x36 — stream enable
 *   0x37 — stream stop
 */

#ifndef DVBSKY_DVBSKY_H
#define DVBSKY_DVBSKY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "usbq/usbq.h"

#ifdef __cplusplus
extern "C" {
#endif

/* USB bulk-IN endpoint for the TS stream on every dvbsky board.
 * Per upstream's DVB_USB_STREAM_BULK(0x82, 8, 4096): 8 URBs of
 * 4096 bytes. */
#define DVBSKY_TS_BULK_EP        0x82
#define DVBSKY_TS_URB_BYTES      4096u
#define DVBSKY_TS_POOL_DEPTH     8u

typedef struct dvbsky_dev dvbsky_dev_t;

/* Open the bridge over an already-claimed usbq device. Caller must
 * have done usbq_open + claim_interface(0). The handle borrows the
 * usbq pointer; caller still owns it.
 *
 * Returns NULL on alloc failure. Open is a pure allocation — no
 * USB traffic; the first dvbsky_gpio_ctrl / i²c xfer is the first
 * thing the chip sees. */
dvbsky_dev_t *dvbsky_open(usbq_dev_t *usb);

/* Close the bridge handle. Does NOT close the usbq device. */
void dvbsky_close(dvbsky_dev_t *dev);

/* GPIO write. `gport` is the upstream gpio-port byte (high nibble
 * = port select, low nibble = pin); `value` is 0 or 1. Mirrors
 * upstream dvbsky_gpio_ctrl. */
int dvbsky_gpio_ctrl(dvbsky_dev_t *dev, uint8_t gport, uint8_t value);

/* Flip the TS bulk-IN endpoint on (1) or off (0). Internally:
 *   off: write [0x37, 0, 0]
 *   on:  write [0x37, 0, 0]; msleep(20); write [0x36, 3, 0]
 *
 * The framework re-fires the on side every time the demod transitions
 * from unlocked → locked, per upstream's `dvbsky_usb_read_status`
 * "resync the slave fifo when signal locks" comment. The engine
 * does that bookkeeping; this primitive is unconditional. */
int dvbsky_stream_ctrl(dvbsky_dev_t *dev, int onoff);

/* Returns the underlying usbq device handle. Borrowed pointer —
 * the dvbsky layer doesn't own the USB device, the caller does. */
usbq_dev_t *dvbsky_usb_handle(dvbsky_dev_t *dev);

/* ---- linuxdvbkpi i²c-adapter shim ----
 *
 * Returns a `struct i2c_adapter *` whose master_xfer routes through
 * the dvbsky i²c-over-bulk protocol. Pass this to
 * `i2c_new_client_device(adap, &board_info)` to attach a lifted chip
 * driver (si2168, si2157/si2141, …).
 *
 * Pointer is owned by the dvbsky_dev_t and stays valid until close.
 * It returns `void *` to keep the public header free of <linux/i2c.h>;
 * cast to `struct i2c_adapter *` at the call site. */
void *dvbsky_get_i2c_adapter(dvbsky_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* DVBSKY_DVBSKY_H */
